#!/usr/bin/env python3
"""Append one Stop-and-Wait experiment run to results.csv."""

from __future__ import annotations

import argparse
import math
import os
import subprocess
from pathlib import Path
from typing import Iterable

CSV_HEADER = (
    "scenario,baud_C,prop_us,ber,payload_B,filesize_B,transfer_s,"
    "R_bps,S_meas,S_theory,a,L_bits,run\n"
)

RX_TIME_FILE = Path("rx_time.txt")
TX_TIME_FILE = Path("tx_time.txt")
PAYLOAD_PATH = Path("penguin.gif")
CSV_PATH = Path("results.csv")


def read_time(path: Path) -> float:
    try:
        return float(path.read_text().strip())
    except FileNotFoundError:
        raise SystemExit(f"error: missing time file {path}")
    except ValueError:
        raise SystemExit(f"error: could not parse float from {path}")


def stat_size(path: Path) -> int:
    """Try platform stat, fall back to os.path.getsize."""
    for cmd in (["stat", "-c%s", str(path)], ["stat", "-f%z", str(path)]):
        try:
            out = subprocess.check_output(cmd, text=True).strip()
            return int(out)
        except (FileNotFoundError, subprocess.CalledProcessError, ValueError):
            continue
    return path.stat().st_size


def ensure_header(path: Path) -> None:
    if path.exists():
        if path.stat().st_size == 0:
            path.write_text(CSV_HEADER)
        return
    path.write_text(CSV_HEADER)


def scenario_choice(value: str) -> str:
    value = value.upper()
    valid = {"FER", "PROP", "BAUD", "PAYLOAD"}
    if value not in valid:
        raise argparse.ArgumentTypeError(f"scenario must be one of {sorted(valid)}")
    return value


def positive_float(value: str) -> float:
    f = float(value)
    if f < 0:
        raise argparse.ArgumentTypeError("value must be >= 0")
    return f


def parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Append a Stop-and-Wait measurement to results.csv"
    )
    parser.add_argument("scenario", type=scenario_choice)
    parser.add_argument("baud_C", type=positive_float, help="channel capacity (baud)")
    parser.add_argument("prop_us", type=positive_float, help="prop delay (us)")
    parser.add_argument("ber", type=positive_float, help="bit error rate")
    parser.add_argument("payload_B", type=positive_float, help="payload size (B)")
    parser.add_argument("run", type=int, help="run index (1..3)")
    parser.add_argument(
        "overhead_bytes",
        nargs="?",
        type=float,
        default=16.0,
        help="overhead bytes per frame (default: 16)",
    )
    parser.add_argument(
        "lbytes_hint",
        nargs="?",
        type=float,
        default=0.0,
        help="override total frame bytes (use payload+overhead when 0)",
    )
    return parser.parse_args(argv)


def main() -> None:
    args = parse_args()
    ensure_header(CSV_PATH)

    # 1) Runtime measurements.
    t_rx = read_time(RX_TIME_FILE)
    t_tx = read_time(TX_TIME_FILE)
    transfer_s = max(t_rx, t_tx)
    if transfer_s <= 0:
        raise SystemExit("error: transfer time must be > 0")

    # 2) Payload stats.
    filesize_B = stat_size(PAYLOAD_PATH)

    overhead_bytes = args.overhead_bytes  # adjust this constant here if protocol changes
    if args.lbytes_hint and args.lbytes_hint > 0:
        frame_bytes = args.lbytes_hint
    else:
        frame_bytes = args.payload_B + overhead_bytes
    L_bits = 10.0 * frame_bytes  # 8N1 serial => 10 bits per byte on the wire

    baud = args.baud_C
    R_bps = (8.0 * filesize_B) / transfer_s
    S_meas = R_bps / baud if baud else math.nan
    Tf = L_bits / baud if baud else math.inf
    a = (args.prop_us / 1e6) / Tf if Tf > 0 else math.inf
    S_theory = (1.0 - args.ber) / (1.0 + 2.0 * a) if math.isfinite(a) else 0.0

    line = (
        f"{args.scenario},{int(baud)},{int(args.prop_us)},{args.ber},"
        f"{int(args.payload_B)},{filesize_B},{transfer_s:.5f},{R_bps:.2f},"
        f"{S_meas:.4f},{S_theory:.4f},{a:.6f},{int(L_bits)},{args.run}\n"
    )
    with CSV_PATH.open("a") as csv_file:
        csv_file.write(line)

    print(line.strip())


if __name__ == "__main__":
    main()
