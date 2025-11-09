#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter, MaxNLocator
from pathlib import Path
import numpy as np

CSV_PATH = Path("results.csv")

# estética base (sem cores fixas)
plt.rcParams.update({
    "figure.figsize": (8.2, 4.8),
    "font.size": 11,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.alpha": 0.28,
    "grid.linestyle": "-",
    "legend.frameon": False,
    "lines.linewidth": 2.2,
})

def agg(df, by):
    g = (df.groupby(by, as_index=False)
           .agg(S_mean=("S_meas","mean"),
                S_std=("S_meas","std"),
                S_theory=("S_theory","mean")))
    g["S_std"] = g["S_std"].fillna(0.0)
    return g.sort_values(by)

def pad_axes(ax, x, y_pad=0.10, x_pad=0.08):
    # margens “respiráveis” sem cortar extremos
    xmin, xmax = np.min(x), np.max(x)
    dx = xmax - xmin if xmax > xmin else (abs(xmax) + 1)  # evita zero
    ax.set_xlim(xmin - dx*x_pad, xmax + dx*x_pad)
    ax.set_ymargin(y_pad)  # padding vertical

def style_y_percent(ax):
    ax.yaxis.set_major_formatter(PercentFormatter(xmax=1.0))
    ax.set_ylim(0, 1.05)       # headroom no topo

def smooth_theory(x, y, factor=250):
    # curva teórica suave sem dependências extra
    xs = np.linspace(np.min(x), np.max(x), factor)
    ys = np.interp(xs, x, y)
    return xs, ys

def plot_fer(df):
    d = df[df["scenario"]=="FER"].copy()
    if d.empty: return
    L_bits = float(d["L_bits"].iloc[0])
    d["FER"] = 1.0 - (1.0 - d["ber"])**L_bits
    d["FER_pct"] = 100.0 * d["FER"]
    g = agg(d, "FER_pct")

    fig, ax = plt.subplots(constrained_layout=True)
    # measured
    ax.errorbar(g["FER_pct"], g["S_mean"], yerr=g["S_std"],
                fmt="o-", capsize=4, capthick=1.2, elinewidth=1.2,
                solid_capstyle="round", solid_joinstyle="round",
                label="Practical Efficiency (S)")
    # theory (suave)
    xs, ys = smooth_theory(g["FER_pct"].to_numpy(), g["S_theory"].to_numpy())
    ax.plot(xs, ys, "--", marker="x", markevery=len(xs)//8, label="Theoretical Efficiency (S)")

    style_y_percent(ax)
    pad_axes(ax, g["FER_pct"].to_numpy(), y_pad=0.12, x_pad=0.10)
    ax.xaxis.set_major_locator(MaxNLocator(6))
    ax.set_xlabel("Frame Error Rate (%)")
    ax.set_ylabel("Efficiency")
    ax.set_title("Efficiency vs Frame Error Rate")
    ax.legend()
    plt.savefig("S_vs_FER_prettier.png", dpi=220); plt.close()

def plot_prop(df):
    d = df[df["scenario"]=="PROP"].copy()
    if d.empty: return
    g = agg(d, "a")

    fig, ax = plt.subplots(constrained_layout=True)
    ax.errorbar(g["a"], g["S_mean"], yerr=g["S_std"],
                fmt="o-", capsize=4, capthick=1.2, elinewidth=1.2,
                solid_capstyle="round", solid_joinstyle="round",
                label="Measured S")
    xs, ys = smooth_theory(g["a"].to_numpy(), g["S_theory"].to_numpy())
    ax.plot(xs, ys, "--", marker="x", markevery=len(xs)//8, label="Theory S")

    style_y_percent(ax)
    pad_axes(ax, g["a"].to_numpy(), y_pad=0.12, x_pad=0.10)
    ax.xaxis.set_major_locator(MaxNLocator(6))
    ax.set_xlabel("a = Tprop / Tf")
    ax.set_ylabel("Efficiency")
    ax.set_title("PROP sweep")
    ax.legend()
    plt.savefig("S_vs_a_prettier.png", dpi=220); plt.close()

def plot_baud(df):
    d = df[df["scenario"]=="BAUD"].copy()
    if d.empty: return
    g = agg(d, "baud_C")

    fig, ax = plt.subplots(constrained_layout=True)
    ax.errorbar(g["baud_C"], g["S_mean"], yerr=g["S_std"],
                fmt="o-", capsize=4, capthick=1.2, elinewidth=1.2,
                solid_capstyle="round", solid_joinstyle="round",
                label="Measured S")
    xs, ys = smooth_theory(g["baud_C"].to_numpy(), g["S_theory"].to_numpy())
    ax.plot(xs, ys, "--", marker="x", markevery=len(xs)//8, label="Theory S")

    style_y_percent(ax)
    pad_axes(ax, g["baud_C"].to_numpy(), y_pad=0.12, x_pad=0.08)
    ax.xaxis.set_major_locator(MaxNLocator(6))
    ax.set_xlabel("Baud rate C (bit/s)")
    ax.set_ylabel("Efficiency")
    ax.set_title("BAUD sweep")
    ax.legend()
    plt.savefig("S_vs_C_prettier.png", dpi=220); plt.close()

def plot_payload(df):
    d = df[df["scenario"]=="PAYLOAD"].copy()
    if d.empty: return
    g = agg(d, "payload_B")

    fig, ax = plt.subplots(constrained_layout=True)
    ax.errorbar(g["payload_B"], g["S_mean"], yerr=g["S_std"],
                fmt="o-", capsize=4, capthick=1.2, elinewidth=1.2,
                solid_capstyle="round", solid_joinstyle="round",
                label="Measured S")
    xs, ys = smooth_theory(g["payload_B"].to_numpy(), g["S_theory"].to_numpy())
    ax.plot(xs, ys, "--", marker="x", markevery=len(xs)//8, label="Theory S")

    style_y_percent(ax)
    pad_axes(ax, g["payload_B"].to_numpy(), y_pad=0.12, x_pad=0.06)
    ax.xaxis.set_major_locator(MaxNLocator(6))
    ax.set_xlabel("Payload (bytes)")
    ax.set_ylabel("Efficiency")
    ax.set_title("PAYLOAD sweep")
    ax.legend()
    plt.savefig("S_vs_Payload_prettier.png", dpi=220); plt.close()

def main():
    if not CSV_PATH.exists():
        raise SystemExit("results.csv not found.")
    df = pd.read_csv(CSV_PATH)
    plot_fer(df)
    plot_prop(df)
    plot_baud(df)
    plot_payload(df)

if __name__ == "__main__":
    main()
