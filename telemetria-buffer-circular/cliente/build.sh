#!/usr/bin/env bash
# Build do cliente C++ com g++ — para maquinas POSIX SEM cmake.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"
mkdir -p "$out"

common=(-std=c++17 -O2 -Wall -I "$here/include")
core=("$here/src/Sensor.cpp" "$here/src/DynamicBuffer.cpp"
      "$here/src/Profiler.cpp" "$here/src/MqttPublisher.cpp")

echo "Compilando cliente..."
g++ "${common[@]}" "${core[@]}" "$here/src/main.cpp" -o "$out/cliente" -pthread

echo "Compilando testes..."
g++ "${common[@]}" "$here/tests/test_buffers.cpp" "$here/src/DynamicBuffer.cpp" \
    -o "$out/test_buffers" -pthread

echo "OK -> $out/cliente ; $out/test_buffers"
