# Telemetria com Buffer Circular — versão cliente-servidor

> **Autores:** Silvio Fittipaldi, Bernardo Heuer e Rodrigo Nunes

Comparação prática entre **alocação dinâmica O(n)** (anti-padrão) e **buffer
circular O(1)** para um fluxo de telemetria de alta frequência, com transmissão
**MQTT**, modelo **produtor-consumidor**, instrumentação de tempo/memória,
**dashboard Node-RED** e relatório em LaTeX.

O enunciado original é embarcado (ESP32). Aqui o hardware é substituído por uma
arquitetura cliente-servidor de software, preservando 100% dos objetivos
didáticos (ver tabela de equivalência no relatório).

```
┌──────────────────────────────┐     MQTT/TCP 1883     ┌────────────────────┐
│        CLIENTE (C++17)        │ ───────────────────► │  Mosquitto (broker) │
│  Sensor → [V1 DynamicBuffer]  │                       └─────────┬──────────┘
│          [V2 RingBuffer]      │                                 │ assina
│  Profiler (tempo µs + memória)│                                 ▼
│  MqttPublisher (sync/async)   │                       ┌────────────────────┐
│  → CSV  → análise (Python)    │                       │  Node-RED  (:1880)  │
└──────────────────────────────┘                       │  Dados + Performance│
                                                        └────────────────────┘
```

## Adaptações de ambiente (decisões de engenharia)

No mesmo espírito da adaptação ESP32 → cliente-servidor:

1. **MQTT autossuficiente** — em vez de Eclipse Paho via vcpkg, a camada MQTT é
   um cliente MQTT 3.1.1 mínimo sobre *sockets* TCP (Winsock/BSD). O projeto
   compila e roda **sem dependências externas**. O contrato didático é o mesmo:
   `publishSync` bloqueia (Vertente 1) e `publishAsync` não bloqueia (Vertente 2).
2. **Build com g++** — há um `CMakeLists.txt` canônico, mas como esta máquina não
   tem CMake, fornecemos scripts `build.ps1`/`build.sh` que usam `g++` direto.

## Pré-requisitos

- **Compilador C++17** (testado com TDM-GCC/MinGW `g++` 10.3). `cmake` é opcional.
- **Docker Desktop** (broker Mosquitto + Node-RED).
- **Python 3** com `pandas` e `matplotlib` (para os gráficos).
- *(Opcional)* uma distribuição **LaTeX** (`pdflatex`) ou **Tectonic** para o relatório.

## Estrutura

```
telemetria-buffer-circular/
├── docker-compose.yml          # mosquitto + node-red
├── cliente/                    # núcleo C++ (estruturas, profiler, MQTT, CLI)
│   ├── include/  src/  tests/
│   ├── CMakeLists.txt          # build canônico (CMake)
│   └── build.ps1 / build.sh    # build direto com g++ (sem CMake)
├── broker/mosquitto.conf
├── dashboard/                  # Dockerfile (node-red + dashboard) + flows.json
├── experimentos/
│   ├── run_benchmarks.py       # roda vertentes × escalas → resultados/summary.csv
│   └── resultados/             # CSVs + dumps de latência
├── analise/
│   ├── requirements.txt  plots.py  figuras/
└── relatorio/                  # relatorio.tex + referencias.bib
```

## Passo a passo

### 1. Subir o servidor (broker + dashboard)

```powershell
docker compose up -d --build
# editor Node-RED: http://localhost:1880   |   dashboard: http://localhost:1880/ui
```

### 2. Compilar o cliente

Com **g++** (sem CMake):
```powershell
powershell -ExecutionPolicy Bypass -File cliente\build.ps1
# gera cliente\build\cliente.exe e cliente\build\test_buffers.exe
```

Ou com **CMake** (caminho canônico):
```powershell
cmake -S cliente -B cliente/build
cmake --build cliente/build
```

### 3. Rodar os testes

```powershell
cliente\build\test_buffers.exe
```

### 4. Rodar um experimento isolado

```powershell
# Vertente 2 (buffer circular O(1)), publicando no broker
cliente\build\cliente.exe --vertente 2 --N 20000 --buffer-cap 4096 `
    --broker tcp://localhost:1883 --publish-every 50 --csv saida.csv

# Vertente 1 (anti-padrão O(n)) com gargalo de rede de 5 ms
cliente\build\cliente.exe --vertente 1 --N 600 --window 200 `
    --net-delay-ms 5 --broker tcp://localhost:1883 --csv saida.csv
```

Verificar as mensagens chegando no broker:
```powershell
docker exec -it mosquitto mosquitto_sub -t "telemetria/#" -v
```

### 5. Rodar TODOS os experimentos e gerar os gráficos

```powershell
python experimentos\run_benchmarks.py        # → experimentos/resultados/summary.csv
pip install -r analise\requirements.txt
python analise\plots.py                       # → analise/figuras/*.png
```

### 6. Compilar o relatório

Com uma distribuição LaTeX tradicional:
```powershell
cd relatorio
pdflatex relatorio.tex ; bibtex relatorio ; pdflatex relatorio.tex ; pdflatex relatorio.tex
```

Ou com **Tectonic** (motor de binário único, baixa os pacotes sob demanda — foi o
usado para gerar `relatorio/relatorio.pdf`):
```powershell
tectonic relatorio.tex   # resolve bibtex e os reruns automaticamente
```

## CLI do cliente

| Flag | Descrição | Padrão |
|---|---|---|
| `--vertente {1,2}` | 1 = DynamicBuffer O(n); 2 = RingBuffer O(1) | 2 |
| `--N <int>` | número de amostras | 5000 |
| `--window <int>` | tamanho da janela (Vertente 1, modo shift) | 1000 |
| `--buffer-cap <int>` | capacidade do RingBuffer (Vertente 2) | 1024 |
| `--dyn-mode {shift,realloc}` | sub-variante da Vertente 1 | shift |
| `--broker <uri>` | `tcp://host:port` do broker | tcp://localhost:1883 |
| `--net-delay-ms <int>` | atraso artificial de rede por publicação | 0 |
| `--publish-every <k>` | publica 1 a cada k amostras | 1 |
| `--batch <int>` | tamanho do dreno em lote (Vertente 2) | 256 |
| `--csv <path>` | arquivo CSV de saída (append) | experimentos/resultados/summary.csv |
| `--tag <str>` | rótulo do experimento (usado na análise) | run |
| `--latency-dump <path>` | grava todas as latências (histograma) | — |
| `--no-mqtt` | desativa o MQTT (benchmark puro de CPU/memória) | — |

## Mapeamento dos entregáveis

**Entregável 1 — Código**
- Duas vertentes selecionáveis por `--vertente`. ✔
- `RingBuffer` é classe própria com `head`/`tail`, O(1), **sem** `erase`/`insert(begin)`/`memmove`/`realloc` (ver `cliente/include/RingBuffer.hpp`). ✔
- Vertente 1 demonstra o anti-padrão: deslocamento O(n) (`shift`) e realloc por amostra (`realloc`). ✔

**Entregável 2 — Painel**
- `telemetria/amostras` → **Gráfico de Dados** no Node-RED. ✔
- `telemetria/performance` → **Gráfico de Performance** (latência por vertente). ✔

**Entregável 3 — Relatório** (`relatorio/relatorio.tex`)
- Análise assintótica O(n) × O(1) (com prova). ✔
- Diagnóstico de memória (contadores de `new`/`delete`). ✔
- Discussão de instabilidade de rede (`--net-delay-ms`). ✔
