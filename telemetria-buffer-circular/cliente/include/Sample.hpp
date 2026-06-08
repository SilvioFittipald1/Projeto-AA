#pragma once
#include <cstdint>
#include <string>

// Amostra de telemetria (equivalente a uma leitura de sensor do ESP32).
struct Sample {
    uint64_t seq;          // numero de sequencia
    double   value;        // valor do "sensor" (ex.: seno + ruido)
    uint64_t t_capture_us; // timestamp de captura em microssegundos
};

// Serializa a amostra como JSON: {"seq":N,"value":x,"t_us":t}
inline std::string toJson(const Sample& s) {
    std::string out;
    out.reserve(64);
    out += "{\"seq\":";
    out += std::to_string(s.seq);
    out += ",\"value\":";
    out += std::to_string(s.value);
    out += ",\"t_us\":";
    out += std::to_string(s.t_capture_us);
    out += "}";
    return out;
}
