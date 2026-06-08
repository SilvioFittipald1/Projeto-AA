#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Orquestrador de experimentos (portavel: Windows e POSIX).

Roda o cliente C++ em todas as combinacoes vertente x escala e grava
experimentos/resultados/summary.csv. Tres experimentos:

  1) ESCALA   - latencia/memoria O(n) x O(1) por N (sem rede; --no-mqtt)
  2) MEMORIA  - variante 'realloc' da Vertente 1 (explosao de alocacoes)
  3) GARGALO  - taxa de amostragem sob atraso de rede (--net-delay-ms)

Tambem grava dumps de latencia (tag 'dist') para o histograma da analise.
"""
import os
import sys
import subprocess
from pathlib import Path

ROOT    = Path(__file__).resolve().parents[1]
CLIENTE = ROOT / "cliente"
BUILD   = CLIENTE / "build"
RESULTS = ROOT / "experimentos" / "resultados"
EXE     = BUILD / ("cliente.exe" if os.name == "nt" else "cliente")
BROKER  = "tcp://localhost:1883"


def ensure_built():
    if EXE.exists():
        return
    print("[build] cliente nao encontrado — compilando...")
    if os.name == "nt":
        subprocess.run(["powershell", "-ExecutionPolicy", "Bypass",
                        "-File", str(CLIENTE / "build.ps1")], check=True)
    else:
        subprocess.run(["bash", str(CLIENTE / "build.sh")], check=True)
    if not EXE.exists():
        sys.exit("[erro] executavel do cliente nao foi gerado.")


def run(args):
    cmd = [str(EXE)] + [str(a) for a in args]
    print(">>", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main():
    ensure_built()
    RESULTS.mkdir(parents=True, exist_ok=True)
    summary = RESULTS / "summary.csv"
    if summary.exists():
        summary.unlink()  # recomeca limpo (o cliente faz append)
    csv = str(summary)

    # ----- 1) ESCALA: O(n) x O(1) em latencia e memoria -----
    print("\n==== Experimento 1: ESCALA ====")
    for N in [100, 5000, 20000, 100000]:
        window = min(2000, max(100, N // 10))
        run(["--vertente", 1, "--dyn-mode", "shift", "--N", N, "--window", window,
             "--no-mqtt", "--csv", csv, "--tag", "escala"])
        run(["--vertente", 2, "--N", N, "--buffer-cap", 4096,
             "--no-mqtt", "--csv", csv, "--tag", "escala"])

    # ----- 2) MEMORIA: variante realloc (custo O(n^2) total -> limita N) -----
    print("\n==== Experimento 2: MEMORIA (realloc) ====")
    for N in [100, 5000, 20000]:
        run(["--vertente", 1, "--dyn-mode", "realloc", "--N", N,
             "--no-mqtt", "--csv", csv, "--tag", "memoria"])

    # ----- 3) GARGALO: taxa de amostragem x atraso de rede -----
    print("\n==== Experimento 3: GARGALO DE REDE ====")
    for d in [0, 1, 2, 5]:
        run(["--vertente", 1, "--N", 600, "--window", 200, "--net-delay-ms", d,
             "--publish-every", 1, "--broker", BROKER, "--csv", csv, "--tag", "gargalo"])
        run(["--vertente", 2, "--N", 600, "--buffer-cap", 256, "--net-delay-ms", d,
             "--publish-every", 1, "--broker", BROKER, "--csv", csv, "--tag", "gargalo"])

    # ----- 4) DUMP de latencias p/ histograma -----
    print("\n==== Dump de latencias (histograma) ====")
    run(["--vertente", 1, "--dyn-mode", "shift", "--N", 20000, "--window", 2000,
         "--no-mqtt", "--csv", csv, "--tag", "dist",
         "--latency-dump", str(RESULTS / "lat_v1_shift.txt")])
    run(["--vertente", 1, "--dyn-mode", "realloc", "--N", 20000,
         "--no-mqtt", "--csv", csv, "--tag", "dist",
         "--latency-dump", str(RESULTS / "lat_v1_realloc.txt")])
    run(["--vertente", 2, "--N", 20000, "--buffer-cap", 4096,
         "--no-mqtt", "--csv", csv, "--tag", "dist",
         "--latency-dump", str(RESULTS / "lat_v2_ring.txt")])

    print("\n[ok] resultados em:", summary)
    print("     proximo passo: python analise/plots.py")


if __name__ == "__main__":
    main()
