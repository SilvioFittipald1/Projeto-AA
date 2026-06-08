# Build do cliente C++ com g++ (TDM-GCC/MinGW) — para maquinas SEM cmake.
# Uso:  powershell -ExecutionPolicy Bypass -File cliente\build.ps1
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$out  = Join-Path $here "build"
if (-not (Test-Path $out)) { New-Item -ItemType Directory -Path $out | Out-Null }

$gpp = "g++"
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    if (Test-Path "C:\TDM-GCC-64\bin\g++.exe") { $gpp = "C:\TDM-GCC-64\bin\g++.exe" }
}

$common = @("-std=c++17","-O2","-Wall","-I", (Join-Path $here "include"))
$core = @(
  (Join-Path $here "src\Sensor.cpp"),
  (Join-Path $here "src\DynamicBuffer.cpp"),
  (Join-Path $here "src\Profiler.cpp"),
  (Join-Path $here "src\MqttPublisher.cpp")
)

Write-Host "Compilando cliente..."
& $gpp @common @core (Join-Path $here "src\main.cpp") `
    -o (Join-Path $out "cliente.exe") -lws2_32 -lpsapi -pthread
if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar o cliente" }

Write-Host "Compilando testes..."
& $gpp @common (Join-Path $here "tests\test_buffers.cpp") (Join-Path $here "src\DynamicBuffer.cpp") `
    -o (Join-Path $out "test_buffers.exe")
if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar os testes" }

Write-Host "OK -> $out\cliente.exe ; $out\test_buffers.exe"
