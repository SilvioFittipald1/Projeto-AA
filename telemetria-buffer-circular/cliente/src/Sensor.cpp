#include "Sensor.hpp"
#include <cmath>

Sensor::Sensor(uint64_t seed)
    : rng_(seed),
      noise_(0.0, 0.05),
      t0_(std::chrono::high_resolution_clock::now()) {}

Sample Sensor::read() {
    Sample s;
    s.seq   = seq_++;
    double phase = static_cast<double>(s.seq) * 0.01;
    s.value = std::sin(phase) + noise_(rng_);
    auto now = std::chrono::high_resolution_clock::now();
    s.t_capture_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now - t0_).count());
    return s;
}
