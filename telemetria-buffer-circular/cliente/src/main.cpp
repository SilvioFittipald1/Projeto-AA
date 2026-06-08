// ============================================================================
// Cliente de Telemetria — orquestrador do experimento.
//   Vertente 1 (sincrona):  captura -> DynamicBuffer O(n) -> publishSync (BLOQUEIA)
//   Vertente 2 (assincrona): produtor -> RingBuffer O(1) | consumidor -> publishAsync
//
// Grava uma linha de metricas no CSV (--csv) e publica:
//   telemetria/amostras     -> {"seq","value","t_us"}  (grafico de dados)
//   telemetria/performance  -> estatisticas agregadas    (grafico de performance)
// ============================================================================
#include "Sample.hpp"
#include "Sensor.hpp"
#include "RingBuffer.hpp"
#include "DynamicBuffer.hpp"
#include "Profiler.hpp"
#include "MqttPublisher.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Config {
    int         vertente   = 2;
    long        N          = 5000;
    long        window     = 1000;            // janela da Vertente 1
    long        bufferCap  = 1024;            // capacidade do RingBuffer (Vertente 2)
    std::string broker     = "tcp://localhost:1883";
    int         netDelayMs = 0;
    std::string csv        = "experimentos/resultados/summary.csv";
    std::string dynMode    = "shift";         // shift | realloc (Vertente 1)
    long        publishEvery = 1;             // publica 1 a cada k amostras
    long        batch      = 256;             // tamanho do dreno (Vertente 2)
    std::string tag        = "run";           // rotulo do experimento p/ a analise
    std::string latDump    = "";              // dump das latencias (histograma)
    bool        noMqtt     = false;
};

std::string argValue(int argc, char** argv, const std::string& key,
                     const std::string& def) {
    for (int i = 1; i < argc - 1; ++i)
        if (key == argv[i]) return argv[i + 1];
    return def;
}
bool hasFlag(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc; ++i) if (key == argv[i]) return true;
    return false;
}

Config parseArgs(int argc, char** argv) {
    Config c;
    c.vertente     = std::atoi(argValue(argc, argv, "--vertente", "2").c_str());
    c.N            = std::atol(argValue(argc, argv, "--N", "5000").c_str());
    c.window       = std::atol(argValue(argc, argv, "--window", "1000").c_str());
    c.bufferCap    = std::atol(argValue(argc, argv, "--buffer-cap", "1024").c_str());
    c.broker       = argValue(argc, argv, "--broker", c.broker);
    c.netDelayMs   = std::atoi(argValue(argc, argv, "--net-delay-ms", "0").c_str());
    c.csv          = argValue(argc, argv, "--csv", c.csv);
    c.dynMode      = argValue(argc, argv, "--dyn-mode", "shift");
    c.publishEvery = std::atol(argValue(argc, argv, "--publish-every", "1").c_str());
    c.batch        = std::atol(argValue(argc, argv, "--batch", "256").c_str());
    c.tag          = argValue(argc, argv, "--tag", "run");
    c.latDump      = argValue(argc, argv, "--latency-dump", "");
    c.noMqtt       = hasFlag(argc, argv, "--no-mqtt");
    if (c.publishEvery < 1) c.publishEvery = 1;
    return c;
}

bool fileHasContent(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f.good() && f.tellg() > 0;
}

void writeCsvRow(const Config& c, const Stats& st, double sampleMs, double totalMs,
                 size_t nAllocs, size_t allocBytes, size_t peakKb,
                 const std::string& modeStr) {
    const std::string header =
        "tag,vertente,mode,N,window,buffer_cap,net_delay_ms,"
        "lat_mean_us,lat_p50_us,lat_p95_us,lat_p99_us,lat_max_us,jitter_us,"
        "sample_ms,tempo_total_ms,throughput_sps,mem_pico_kb,n_allocs,alloc_bytes\n";
    bool exists = fileHasContent(c.csv);
    std::ofstream f(c.csv, std::ios::app);
    if (!exists) f << header;
    double thr = sampleMs > 0 ? (c.N / (sampleMs / 1000.0)) : 0.0; // taxa de amostragem
    f << c.tag << ',' << c.vertente << ',' << modeStr << ',' << c.N << ','
      << c.window << ',' << c.bufferCap << ',' << c.netDelayMs << ','
      << st.mean << ',' << st.p50 << ',' << st.p95 << ',' << st.p99 << ','
      << st.max << ',' << st.stddev << ',' << sampleMs << ',' << totalMs << ','
      << thr << ',' << peakKb << ',' << nAllocs << ',' << allocBytes << '\n';
}

void dumpLatencies(const std::string& path, const std::vector<double>& lat) {
    if (path.empty()) return;
    std::ofstream f(path);
    for (double x : lat) f << x << '\n';
}

void logLine(uint64_t latUs) {
    std::printf("Latencia: %llu us | Allocs vivas: %zu | Bytes: %zu\n",
                static_cast<unsigned long long>(latUs),
                Profiler::liveAllocs(), Profiler::liveBytes());
}

} // namespace

int main(int argc, char** argv) {
    Config c = parseArgs(argc, argv);

    Sensor   sensor;
    Profiler prof;
    prof.reserve(static_cast<size_t>(c.N));

    std::string clientId = "telemetria-v" + std::to_string(c.vertente);
    MqttPublisher* mqtt = c.noMqtt
        ? nullptr
        : new MqttPublisher(c.broker, clientId, c.netDelayMs);

    const std::string TOPIC_DATA = "telemetria/amostras";
    const std::string TOPIC_PERF = "telemetria/performance";
    const long logEvery = c.N >= 10 ? c.N / 10 : 1;

    std::string modeStr;
    size_t ctorAllocs = 0, ctorBytes = 0;

    // t0 = inicio | sampleEndUs = fim da AMOSTRAGEM (produtor) | t1 = fim total
    // (inclui o dreno/flush da rede). A taxa de amostragem usa sampleEndUs: e o
    // que mostra a Vertente 1 (sincrona) colapsar e a Vertente 2 absorver.
    uint64_t t0 = Profiler::nowMicros();
    uint64_t sampleEndUs = 0;

    if (c.vertente == 1) {
        // -------- Vertente 1: SINCRONA (loop unico) --------
        DynMode m = (c.dynMode == "realloc") ? DynMode::REALLOC : DynMode::SHIFT;
        modeStr   = (m == DynMode::REALLOC) ? "realloc" : "shift";

        size_t a0 = Profiler::totalAllocs(), b0 = Profiler::totalAllocBytes();
        DynamicBuffer buf(static_cast<size_t>(c.window), m);
        ctorAllocs = Profiler::totalAllocs() - a0;
        ctorBytes  = Profiler::totalAllocBytes() - b0;

        for (long i = 0; i < c.N; ++i) {
            Sample s = sensor.read();
            prof.markInsertStart();
            buf.insert(s);                 // O(n)
            prof.markInsertEnd();
            if (mqtt && (i % c.publishEvery == 0))
                mqtt->publishSync(TOPIC_DATA, toJson(s)); // BLOQUEIA
            if (i % logEvery == 0 && !prof.latencies().empty())
                logLine(static_cast<uint64_t>(prof.latencies().back()));
        }
        sampleEndUs = Profiler::nowMicros(); // amostragem == loop (acoplada a rede)
    } else {
        // -------- Vertente 2: ASSINCRONA (produtor + consumidor) --------
        modeStr = "ring";
        size_t a0 = Profiler::totalAllocs(), b0 = Profiler::totalAllocBytes();
        RingBuffer<Sample> ring(static_cast<size_t>(c.bufferCap));
        ctorAllocs = Profiler::totalAllocs() - a0;
        ctorBytes  = Profiler::totalAllocBytes() - b0;

        std::mutex              m;
        std::condition_variable cv;
        std::atomic<bool>       producing{true};
        std::atomic<uint64_t>   prodEndUs{0};

        std::thread producer([&] {
            for (long i = 0; i < c.N; ++i) {
                Sample s = sensor.read();
                {
                    std::lock_guard<std::mutex> lk(m);
                    prof.markInsertStart();
                    ring.push(s);          // O(1) — nunca bloqueia pela rede
                    prof.markInsertEnd();
                }
                cv.notify_one();
            }
            prodEndUs.store(Profiler::nowMicros()); // fim da amostragem (produtor)
            producing.store(false);
            cv.notify_all();
        });

        std::thread consumer([&] {
            std::vector<Sample> drained;
            drained.reserve(static_cast<size_t>(c.batch));
            for (;;) {
                {
                    std::unique_lock<std::mutex> lk(m);
                    cv.wait(lk, [&] { return !ring.empty() || !producing.load(); });
                    if (ring.empty() && !producing.load()) break;
                    ring.popBatch(drained, static_cast<size_t>(c.batch));
                }
                for (const Sample& s : drained)
                    if (mqtt && (s.seq % c.publishEvery == 0))
                        mqtt->publishAsync(TOPIC_DATA, toJson(s)); // NAO bloqueia
                drained.clear();
            }
        });

        producer.join();
        sampleEndUs = prodEndUs.load(); // amostragem termina com o produtor
        consumer.join();

        if (ring.overwrites() > 0)
            std::printf("[Vertente 2] RingBuffer sobrescreveu %zu amostras "
                        "(consumidor nao acompanhou — politica: descarta o mais antigo)\n",
                        ring.overwrites());
        if (!prof.latencies().empty())
            logLine(static_cast<uint64_t>(prof.latencies().back()));
    }

    if (mqtt) mqtt->flush();
    uint64_t t1 = Profiler::nowMicros();

    if (sampleEndUs == 0) sampleEndUs = t1;
    double  totalMs    = (t1 - t0) / 1000.0;
    double  sampleMs   = (sampleEndUs - t0) / 1000.0;
    Stats   st         = prof.summarize();
    size_t  nAllocs    = ctorAllocs + prof.insertAllocs();
    size_t  allocBytes = ctorBytes  + prof.insertAllocBytes();
    size_t  peakKb     = Profiler::peakRssKB();
    double  thr        = sampleMs > 0 ? c.N / (sampleMs / 1000.0) : 0.0;

    // Relatorio no console
    std::printf("\n===== RESULTADO (Vertente %d / %s) =====\n", c.vertente, modeStr.c_str());
    std::printf("N=%ld  janela=%ld  cap=%ld  net_delay=%dms\n",
                c.N, c.window, c.bufferCap, c.netDelayMs);
    std::printf("Latencia de insercao (us): media=%.3f p50=%.3f p95=%.3f p99=%.3f max=%.3f jitter=%.3f\n",
                st.mean, st.p50, st.p95, st.p99, st.max, st.stddev);
    std::printf("Amostragem: %.2f ms (%.0f amostras/s) | Tempo total c/ rede: %.2f ms\n",
                sampleMs, thr, totalMs);
    std::printf("Memoria: n_allocs(estrutura)=%zu  alloc_bytes=%zu  pico_processo=%zu KB\n",
                nAllocs, allocBytes, peakKb);
    if (mqtt) std::printf("MQTT: conectado=%s  publicadas=%llu\n",
                          mqtt->connected() ? "sim" : "nao",
                          static_cast<unsigned long long>(mqtt->sent()));

    writeCsvRow(c, st, sampleMs, totalMs, nAllocs, allocBytes, peakKb, modeStr);
    dumpLatencies(c.latDump, prof.latencies());

    // Publica o resumo de performance (alimenta o grafico de performance)
    if (mqtt) {
        std::string perf =
            "{\"vertente\":" + std::to_string(c.vertente) +
            ",\"mode\":\"" + modeStr + "\"" +
            ",\"N\":" + std::to_string(c.N) +
            ",\"lat_mean_us\":" + std::to_string(st.mean) +
            ",\"lat_p95_us\":"  + std::to_string(st.p95) +
            ",\"jitter_us\":"   + std::to_string(st.stddev) +
            ",\"tempo_total_ms\":" + std::to_string(totalMs) +
            ",\"n_allocs\":"    + std::to_string(nAllocs) +
            ",\"mem_pico_kb\":" + std::to_string(peakKb) + "}";
        mqtt->publishSync(TOPIC_PERF, perf);
        mqtt->flush();
        delete mqtt;
    }
    return 0;
}
