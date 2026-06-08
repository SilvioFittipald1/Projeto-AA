cmake_minimum_required(VERSION 3.16)
project(telemetria_cliente CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CORE_SOURCES
  src/Sensor.cpp
  src/DynamicBuffer.cpp
  src/Profiler.cpp
  src/MqttPublisher.cpp)

# ---- Executavel principal ----
add_executable(cliente ${CORE_SOURCES} src/main.cpp)

# ---- Testes ----
enable_testing()
add_executable(test_buffers tests/test_buffers.cpp src/DynamicBuffer.cpp)
add_test(NAME buffers COMMAND test_buffers)

# ---- Threads + sockets por plataforma ----
find_package(Threads REQUIRED)
target_link_libraries(cliente PRIVATE Threads::Threads)
target_link_libraries(test_buffers PRIVATE Threads::Threads)

if(WIN32)
  target_link_libraries(cliente PRIVATE ws2_32 psapi)
endif()

# ----------------------------------------------------------------------------
# NOTA (adaptacao de ambiente): o MqttPublisher e autossuficiente (cliente MQTT
# minimo sobre sockets), portanto este projeto NAO exige vcpkg nem Paho.
# Para a variante canonica com Eclipse Paho (paho-mqttpp3), instale via vcpkg e
# ligue manualmente — a interface publishSync/publishAsync e a mesma.
# ----------------------------------------------------------------------------
