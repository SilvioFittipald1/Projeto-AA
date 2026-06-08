#pragma once
#include "Sample.hpp"
#include <random>
#include <chrono>

// Simulador de sensor de alta frequencia (substitui a captura no ESP32).
// Gera valor = sin(t) + ruido gaussiano e timestamp em microssegundos.
class Sensor {
public:
    explicit Sensor(uint64_t seed = 0xC0FFEEULL);
    Sample read();

private:
    uint64_t seq_ = 0;
    std::mt19937_64 rng_;
    std::normal_distribution<double> noise_;
    std::chrono::high_resolution_clock::time_point t0_;
};
