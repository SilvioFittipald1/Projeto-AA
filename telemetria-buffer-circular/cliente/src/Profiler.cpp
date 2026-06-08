#include "Profiler.hpp"

#include <atomic>
#include <cstdlib>
#include <new>
#include <algorithm>
#include <cmath>
#include <chrono>

#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
#else
  #include <sys/resource.h>
#endif

// ----------------------------------------------------------------------------
// Contadores globais de alocacao.
// Sobrescrevemos os operadores globais new/delete para instrumentar TODA
// alocacao dinamica do processo. Cada bloco recebe um cabecalho de HDR bytes
// onde guardamos o tamanho solicitado, permitindo descontar no delete e medir
// bytes vivos / pico. HDR = 16 preserva o alinhamento de max_align_t (x64).
// ----------------------------------------------------------------------------
namespace {
    std::atomic<size_t> g_totalAllocs{0};
    std::atomic<size_t> g_totalBytes {0};
    std::atomic<size_t> g_liveAllocs {0};
    std::atomic<size_t> g_liveBytes  {0};
    std::atomic<size_t> g_peakBytes  {0};

    constexpr size_t HDR = 16;

    inline void* trackedAlloc(size_t size) {
        void* base = std::malloc(size + HDR);
        if (!base) return nullptr;
        *reinterpret_cast<size_t*>(base) = size;
        g_totalAllocs.fetch_add(1, std::memory_order_relaxed);
        g_totalBytes.fetch_add(size, std::memory_order_relaxed);
        g_liveAllocs.fetch_add(1, std::memory_order_relaxed);
        size_t live = g_liveBytes.fetch_add(size, std::memory_order_relaxed) + size;
        size_t prev = g_peakBytes.load(std::memory_order_relaxed);
        while (live > prev &&
               !g_peakBytes.compare_exchange_weak(prev, live, std::memory_order_relaxed)) {
            // prev foi atualizado por compare_exchange_weak; tenta de novo
        }
        return reinterpret_cast<char*>(base) + HDR;
    }

    inline void trackedFree(void* ptr) {
        if (!ptr) return;
        void* base = reinterpret_cast<char*>(ptr) - HDR;
        size_t size = *reinterpret_cast<size_t*>(base);
        g_liveAllocs.fetch_sub(1, std::memory_order_relaxed);
        g_liveBytes.fetch_sub(size, std::memory_order_relaxed);
        std::free(base);
    }
}

void* operator new(std::size_t size)                              { void* p = trackedAlloc(size); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t size)                            { void* p = trackedAlloc(size); if (!p) throw std::bad_alloc(); return p; }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept   { return trackedAlloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return trackedAlloc(size); }

void operator delete(void* p) noexcept                           { trackedFree(p); }
void operator delete[](void* p) noexcept                         { trackedFree(p); }
void operator delete(void* p, std::size_t) noexcept              { trackedFree(p); }
void operator delete[](void* p, std::size_t) noexcept            { trackedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept    { trackedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept  { trackedFree(p); }

// ----------------------------------------------------------------------------
// Profiler
// ----------------------------------------------------------------------------
void Profiler::reserve(size_t n) { lat_us_.reserve(n); }

// Relogio em nanossegundos de alta resolucao. No Windows usamos
// QueryPerformanceCounter (o high_resolution_clock do MinGW e grosseiro,
// ~1 ms — zeraria tanto a latencia por insercao quanto duracoes sub-ms);
// em POSIX, steady_clock. Aritmetica inteira relativa a uma base para
// preservar precisao. Tudo (latencia, tempo total, amostragem) deriva daqui.
#ifdef _WIN32
static inline uint64_t nowNanos() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    static LARGE_INTEGER base = [] { LARGE_INTEGER b; QueryPerformanceCounter(&b);   return b; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    uint64_t delta = static_cast<uint64_t>(c.QuadPart - base.QuadPart);
    return (delta * 1000000000ULL) / static_cast<uint64_t>(freq.QuadPart);
}
#else
static inline uint64_t nowNanos() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
#endif

uint64_t Profiler::nowMicros() { return nowNanos() / 1000ULL; }

void Profiler::markInsertStart() {
    aStart_ = g_totalAllocs.load(std::memory_order_relaxed);
    bStart_ = g_totalBytes.load(std::memory_order_relaxed);
    tStart_ = nowNanos();
}

void Profiler::markInsertEnd() {
    uint64_t end = nowNanos();
    lat_us_.push_back(static_cast<double>(end - tStart_) / 1000.0); // ns -> us
    insertAllocs_ += g_totalAllocs.load(std::memory_order_relaxed) - aStart_;
    insertBytes_  += g_totalBytes.load(std::memory_order_relaxed) - bStart_;
}

Stats Profiler::summarize() const {
    Stats st;
    if (lat_us_.empty()) return st;
    std::vector<double> v = lat_us_;          // copia para ordenar (pos-medicao)
    std::sort(v.begin(), v.end());
    st.count = v.size();

    double sum = 0;
    for (double x : v) sum += x;
    st.mean = sum / v.size();

    auto pct = [&](double p) -> double {
        size_t idx = static_cast<size_t>(std::ceil(p * (v.size() - 1)));
        return v[idx];
    };
    st.p50 = pct(0.50);
    st.p95 = pct(0.95);
    st.p99 = pct(0.99);
    st.max = v.back();

    double var = 0;
    for (double x : v) { double d = x - st.mean; var += d * d; }
    var /= v.size();
    st.stddev = std::sqrt(var);
    return st;
}

size_t Profiler::totalAllocs()     { return g_totalAllocs.load(); }
size_t Profiler::totalAllocBytes() { return g_totalBytes.load();  }
size_t Profiler::liveAllocs()      { return g_liveAllocs.load();  }
size_t Profiler::liveBytes()       { return g_liveBytes.load();   }
size_t Profiler::peakLiveBytes()   { return g_peakBytes.load();   }

size_t Profiler::peakRssKB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.PeakWorkingSetSize / 1024);
    return 0;
#elif defined(__APPLE__)
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    return static_cast<size_t>(ru.ru_maxrss / 1024); // macOS: bytes -> KB
#else
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    return static_cast<size_t>(ru.ru_maxrss);        // Linux: ja em KB
#endif
}
