#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gera as figuras do relatorio a partir de experimentos/resultados/summary.csv:

  fig_latencia_vs_N.png   - latencia media de insercao x N (O(n) x O(1))
  fig_jitter_vs_N.png     - jitter (stddev) x N
  fig_memoria_vs_N.png    - n. de alocacoes x N (shift/ring/realloc)
  fig_bytes_vs_N.png      - bytes alocados x N
  fig_gargalo.png         - taxa de amostragem x atraso de rede
  fig_dist_latencia.png   - distribuicao da latencia por insercao (histograma)
"""
import sys
from pathlib import Path

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT    = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "experimentos" / "resultados"
FIG     = ROOT / "analise" / "figuras"
SUMMARY = RESULTS / "summary.csv"


def save(name):
    FIG.mkdir(parents=True, exist_ok=True)
    out = FIG / name
    plt.tight_layout()
    plt.savefig(out, dpi=130)
    plt.close()
    print("  ->", out.name)


def label(v, mode):
    return f"V{v} ({mode})"


def plot_latency_vs_N(df):
    d = df[df.tag == "escala"]
    if d.empty:
        return
    plt.figure(figsize=(7, 5))
    for (v, mode), g in d.groupby(["vertente", "mode"]):
        g = g.sort_values("N")
        plt.plot(g.N, g.lat_mean_us, marker="o", label=label(v, mode))
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("N (numero de amostras)")
    plt.ylabel("Latencia media de insercao (us)")
    plt.title("Latencia de insercao x N  —  O(n) x O(1)")
    plt.grid(True, which="both", ls=":")
    plt.legend()
    save("fig_latencia_vs_N.png")


def plot_jitter_vs_N(df):
    d = df[df.tag == "escala"]
    if d.empty:
        return
    plt.figure(figsize=(7, 5))
    for (v, mode), g in d.groupby(["vertente", "mode"]):
        g = g.sort_values("N")
        plt.plot(g.N, g.jitter_us, marker="s", label=label(v, mode))
    plt.xscale("log")
    plt.xlabel("N (numero de amostras)")
    plt.ylabel("Jitter — desvio-padrao da latencia (us)")
    plt.title("Jitter x N")
    plt.grid(True, which="both", ls=":")
    plt.legend()
    save("fig_jitter_vs_N.png")


def plot_memory_vs_N(df):
    d = df[df.tag.isin(["escala", "memoria"])]
    if d.empty:
        return
    # n_allocs x N
    plt.figure(figsize=(7, 5))
    for (v, mode), g in d.groupby(["vertente", "mode"]):
        g = g.sort_values("N")
        plt.plot(g.N, g.n_allocs.clip(lower=1), marker="o", label=label(v, mode))
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("N (numero de amostras)")
    plt.ylabel("Numero de alocacoes (estrutura)")
    plt.title("Alocacoes x N  —  crescimento x alocacao unica")
    plt.grid(True, which="both", ls=":")
    plt.legend()
    save("fig_memoria_vs_N.png")

    # bytes x N
    plt.figure(figsize=(7, 5))
    for (v, mode), g in d.groupby(["vertente", "mode"]):
        g = g.sort_values("N")
        plt.plot(g.N, g.alloc_bytes.clip(lower=1), marker="o", label=label(v, mode))
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("N (numero de amostras)")
    plt.ylabel("Bytes alocados (acumulado)")
    plt.title("Bytes alocados x N")
    plt.grid(True, which="both", ls=":")
    plt.legend()
    save("fig_bytes_vs_N.png")


def plot_gargalo(df):
    d = df[df.tag == "gargalo"]
    if d.empty:
        return
    plt.figure(figsize=(7, 5))
    for (v, mode), g in d.groupby(["vertente", "mode"]):
        g = g.sort_values("net_delay_ms")
        plt.plot(g.net_delay_ms, g.throughput_sps, marker="o", label=label(v, mode))
    plt.yscale("log")
    plt.xlabel("Atraso artificial de rede por publicacao (ms)")
    plt.ylabel("Taxa de amostragem (amostras/s)")
    plt.title("Instabilidade de rede: amostragem sincrona x assincrona")
    plt.grid(True, which="both", ls=":")
    plt.legend()
    save("fig_gargalo.png")


def plot_distribution():
    files = {
        "V1 (shift)":   RESULTS / "lat_v1_shift.txt",
        "V1 (realloc)": RESULTS / "lat_v1_realloc.txt",
        "V2 (ring)":    RESULTS / "lat_v2_ring.txt",
    }
    series = {}
    for k, p in files.items():
        if p.exists():
            vals = [float(x) for x in p.read_text().split() if x.strip()]
            if vals:
                series[k] = vals
    if not series:
        return
    plt.figure(figsize=(7, 5))
    # recorta no p99 global p/ legibilidade (caudas muito longas no realloc)
    allv = sorted(v for s in series.values() for v in s)
    hi = allv[int(0.99 * (len(allv) - 1))] if allv else 1.0
    hi = max(hi, 0.5)
    for k, vals in series.items():
        clipped = [min(v, hi) for v in vals]
        plt.hist(clipped, bins=60, alpha=0.5, label=k)
    plt.yscale("log")
    plt.xlabel(f"Latencia por insercao (us)  [recortada em p99 = {hi:.2f} us]")
    plt.ylabel("Frequencia (escala log)")
    plt.title("Distribuicao da latencia de insercao")
    plt.legend()
    save("fig_dist_latencia.png")


def main():
    if not SUMMARY.exists():
        sys.exit(f"Nao encontrei {SUMMARY}. Rode 'python experimentos/run_benchmarks.py' antes.")
    df = pd.read_csv(SUMMARY)
    print("Gerando figuras em", FIG)
    plot_latency_vs_N(df)
    plot_jitter_vs_N(df)
    plot_memory_vs_N(df)
    plot_gargalo(df)
    plot_distribution()
    print("[ok] figuras geradas.")


if __name__ == "__main__":
    main()
