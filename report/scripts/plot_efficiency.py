#!/usr/bin/env python3
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

CSV_PATH = Path("results.csv")
PLOTS = [
    ("FER", "ber", "S_vs_FER.png", "FER (bit error rate)"),
    ("PROP", "a", "S_vs_a.png", "a = Tprop / Tf"),
    ("BAUD", "baud_C", "S_vs_C.png", "Baud rate C (bit/s)"),
    ("PAYLOAD", "payload_B", "S_vs_Payload.png", "Payload (bytes)"),
]
SERIES_STYLE = (
    ("S_meas", "Measured S", "#0072B2", "o", "-"),
    ("S_theory", "Theory S", "#D55E00", "x", "--"),
)
FIG_SIZE = (7.5, 4.2)
FACE_COLOR = "#f8f9fb"
plt.style.use("ggplot")
plt.rcParams.update(
    {
        "axes.edgecolor": "#b0b0b0",
        "axes.labelweight": "bold",
        "axes.titleweight": "bold",
        "grid.alpha": 0.45,
        "grid.linestyle": ":",
        "grid.linewidth": 0.8,
        "legend.frameon": False,
    }
)

def plot_subset(df: pd.DataFrame, scenario: str, xcol: str, outfile: str, xlabel: str) -> None:
    subset = df[df["scenario"] == scenario].copy()
    if subset.empty:
        print(f"[warn] skipping {outfile} - no rows for scenario={scenario}")
        return
    subset.sort_values(xcol, inplace=True)

    fig, ax = plt.subplots(figsize=FIG_SIZE, dpi=160)
    ax.set_facecolor(FACE_COLOR)

    for col, label, color, marker, linestyle in SERIES_STYLE:
        ax.plot(
            subset[xcol],
            subset[col],
            label=label,
            color=color,
            marker=marker,
            linestyle=linestyle,
            linewidth=2.2,
            markersize=6,
        )

    ax.set_xlabel(xlabel)
    ax.set_ylabel("Efficiency S")
    ax.set_title(f"{scenario} sweep")
    ax.grid(True, which="both")
    ax.margins(x=0.02, y=0.05)
    ax.tick_params(axis="x", rotation=0)
    ax.minorticks_on()
    for spine in ax.spines.values():
        spine.set_alpha(0.5)

    ax.legend()
    fig.tight_layout()
    fig.savefig(outfile)
    plt.close(fig)
    print(f"saved plot -> {outfile}")

def main() -> None:
    if not CSV_PATH.exists():
        raise SystemExit("error: results.csv not found. Run add_result.py first.")
    df = pd.read_csv(CSV_PATH)
    for scenario, xcol, outfile, xlabel in PLOTS:
        plot_subset(df, scenario, xcol, outfile, xlabel)

if __name__ == "__main__":
    main()
