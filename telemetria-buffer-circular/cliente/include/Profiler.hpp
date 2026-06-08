#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Estatisticas de latencia (em microssegundos). jitter = stddev.
struct Stats {
    double  mean   = 0;
    double  p50    = 0;
    double  p95    = 0;
    double  p99    = 0;
    double  max    = 0;
    double  stddev = 0; // jitter
    size_t  count  = 0;
};

// ============================================================================
// Profiler — instrumentacao de tempo e memoria.
// Substitui micros() e ESP.getFreeHeap()/fragmentacao de heap do enunciado:
//   - tempo: std::chrono::high_resolution_clock (resolucao de us)
//   - memoria (metrica PRIMARIA e 100% portavel): contadores globais de
//     operator new / operator delete (definidos em Profiler.cpp).
//   - pico de memoria do processo (secundario): GetProcessMemoryInfo (Windows)
//     ou getrusage (POSIX).
//
// markInsertStart()/markInsertEnd() medem a latencia de UMA insercao na
// estrutura E atribuem a essa insercao as alocacoes ocorridas no intervalo,
// isolando o custo de memoria da estrutura de dados (sem contar MQTT/setup).
// ============================================================================
class Profiler {
public:
    void   reserve(size_t n);     // pre-aloca o vetor de latencias (evita ruido)
    void   markInsertStart();
    void   markInsertEnd();
    Stats  summarize() const;

    const std::vector<double>& latencies() const { return lat_us_; }
    size_t insertAllocs()     const { return insertAllocs_; }
    size_t insertAllocBytes() const { return insertBytes_;  }

    // Relogio de alta resolucao (us desde epoch).
    static uint64_t nowMicros();

    // Contadores globais de alocacao (metrica primaria, portavel).
    static size_t totalAllocs();      // numero acumulado de operator new
    static size_t totalAllocBytes();  // bytes acumulados solicitados
    static size_t liveAllocs();       // alocacoes ainda nao liberadas
    static size_t liveBytes();        // bytes vivos no momento
    static size_t peakLiveBytes();    // pico de bytes vivos (alta marca)

    // Pico de memoria do processo (secundario, por SO).
    static size_t peakRssKB();

private:
    std::vector<double> lat_us_;
    uint64_t tStart_ = 0;
    size_t   aStart_ = 0, bStart_ = 0;
    size_t   insertAllocs_ = 0, insertBytes_ = 0;
};
