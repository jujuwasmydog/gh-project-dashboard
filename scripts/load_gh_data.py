#!/usr/bin/env python3
"""Load greenhouse-telemetry CSVs and write per-file summary reports.

Run with no args from a directory containing one or more gh_data CSVs.
The script lists the available files, prompts you to pick one, several,
or all, and writes a timestamped .txt report for each selected file.

Handles two layouts:
  - master gh_data.csv: no top header; six embedded header rows scattered
    through the file; 68 columns including a mostly-empty raw_json.
  - daily splits (gh_data_YYYY-MM-DD.csv): proper header row; 67 columns.
"""

from __future__ import annotations

import contextlib
import datetime as dt
import sys
from pathlib import Path

import pandas as pd

DEFAULT_CSV = Path(__file__).resolve().parent / "gh_data.csv"

COLUMNS = [
    "ts", "t1", "t2", "tavg", "h1", "h2", "havg", "soil", "lux",
    "louvre_pct", "louvre_state", "fan_pct", "lightOn",
    "fault_count", "system_fault", "mode", "irrigation_mode",
    "temp_f", "rh_pct", "tank_temp_f",
    "soil1_raw", "soil1_pct", "soil2_raw", "soil2_pct",
    "soil3_raw", "soil3_pct", "soil4_raw", "soil4_pct",
    "batt_v", "pv_v", "pv_present", "louvre_pos_pct",
    "row1", "row2", "row3", "row4",
    "row1_en", "row2_en", "row3_en", "row4_en",
    "row1_ms", "row2_ms", "row3_ms", "row4_ms",
    "row1_start_pct", "row2_start_pct", "row3_start_pct", "row4_start_pct",
    "row1_stop_pct", "row2_stop_pct", "row3_stop_pct", "row4_stop_pct",
    "pump_on", "heater_on", "light_on", "fan_pwm",
    "rows_run_active", "rows_run_auto", "active_row",
    "temp_open_f", "temp_close_f", "rh_open_pct",
    "tank_heat_on_f", "tank_heat_off_f", "schedule_interval_ms",
    "eco_mode", "controller_fault", "raw_json",
]

CATEGORICAL = {"mode", "irrigation_mode", "louvre_state"}


def load(path: Path = DEFAULT_CSV) -> pd.DataFrame:
    """Return a DataFrame indexed by sample timestamp (UTC).

    Auto-detects whether the file already has a header row.
    """
    first_cell = pd.read_csv(path, header=None, nrows=1, dtype=str).iloc[0, 0]
    if first_cell == "ts":
        df = pd.read_csv(path, dtype=str, low_memory=False)
    else:
        df = pd.read_csv(
            path, header=None, names=COLUMNS, dtype=str, low_memory=False,
        )

    # Drop any embedded header rows (ts is the literal string "ts" there)
    ts_num = pd.to_numeric(df["ts"], errors="coerce")
    df = df.loc[ts_num.notna()].copy()

    for col in df.columns:
        if col in CATEGORICAL or col == "raw_json":
            continue
        df[col] = pd.to_numeric(df[col], errors="coerce")

    df["ts"] = pd.to_datetime(df["ts"].astype("int64"), unit="ms", utc=True)
    df = df.set_index("ts").sort_index()
    if "raw_json" in df.columns:
        df = df.drop(columns=["raw_json"])
    return df


def _likely_installed(s: pd.Series) -> bool:
    """Return False if the column is all zero / all null — i.e. sensor unused."""
    nonzero = s.fillna(0) != 0
    return bool(nonzero.any())


def summarize(df: pd.DataFrame) -> None:
    span = df.index.max() - df.index.min()
    interval_s = df.index.to_series().diff().dt.total_seconds().median()

    print("=" * 64)
    print("OVERVIEW")
    print("=" * 64)
    print(f"Rows (data only):       {len(df):,}")
    print(f"Columns:                {df.shape[1]}")
    print(f"Time range (UTC):       {df.index.min()}")
    print(f"                   →    {df.index.max()}")
    print(f"Duration:               {span}")
    print(f"Median sample interval: {interval_s:.1f} s")

    print("\n" + "=" * 64)
    print("SENSORS — installed inference (any non-zero, non-null reading)")
    print("=" * 64)
    sensor_cols = [
        "t1", "t2", "tavg", "h1", "h2", "havg", "soil", "lux",
        "tank_temp_f", "batt_v", "pv_v",
        "soil1_raw", "soil2_raw", "soil3_raw", "soil4_raw",
    ]
    for c in sensor_cols:
        if c in df.columns:
            mark = "yes" if _likely_installed(df[c]) else " no"
            print(f"  [{mark}]  {c}")

    print("\n" + "=" * 64)
    print("MODE DISTRIBUTION")
    print("=" * 64)
    for col in ("mode", "irrigation_mode", "louvre_state"):
        if col in df.columns:
            print(f"\n{col}:")
            print(df[col].value_counts(dropna=False).to_string())

    print("\n" + "=" * 64)
    print("FAULTS")
    print("=" * 64)
    if "fault_count" in df.columns:
        s = df["fault_count"]
        print(f"  fault_count       median={s.median():.0f}  "
              f"max={s.max():.0f}  unique={s.nunique()}")
    if "system_fault" in df.columns:
        s = df["system_fault"].fillna(0).astype(int)
        print(f"  system_fault      any={bool(s.any())}  "
              f"fraction_set={s.mean():.2%}")
    if "controller_fault" in df.columns:
        s = df["controller_fault"].fillna(0).astype(int)
        print(f"  controller_fault  non-zero rows: {(s != 0).sum():,}")

    print("\n" + "=" * 64)
    print("IRRIGATION ACTIVITY")
    print("=" * 64)
    row_cols = [c for c in (f"row{i}" for i in (1, 2, 3, 4)) if c in df.columns]
    if row_cols:
        any_row = df[row_cols].fillna(0).astype(bool).any(axis=1)
        print(f"  samples with ANY row active: "
              f"{int(any_row.sum()):,} / {len(df):,}  ({any_row.mean():.2%})")
        for c in row_cols:
            on = int(df[c].fillna(0).astype(bool).sum())
            print(f"    {c} active: {on:,} samples")
    if "active_row" in df.columns:
        print("\n  active_row distribution:")
        print(df["active_row"].value_counts(dropna=False).sort_index().to_string())
    if "pump_on" in df.columns:
        on = int(df["pump_on"].fillna(0).astype(bool).sum())
        print(f"\n  pump_on samples:    {on:,}")
    if "heater_on" in df.columns:
        on = int(df["heater_on"].fillna(0).astype(bool).sum())
        print(f"  heater_on samples:  {on:,}")
    if "light_on" in df.columns:
        on = int(df["light_on"].fillna(0).astype(bool).sum())
        print(f"  light_on samples:   {on:,}")

    print("\n" + "=" * 64)
    print("NUMERIC RANGES (selected sensors)")
    print("=" * 64)
    interesting = [
        "t1", "t2", "tavg", "temp_f",
        "h1", "h2", "havg", "rh_pct",
        "tank_temp_f", "lux",
        "soil", "soil1_pct", "soil2_pct", "soil3_pct", "soil4_pct",
        "batt_v", "pv_v",
        "fan_pct", "fan_pwm", "louvre_pct", "louvre_pos_pct",
    ]
    print(f"  {'column':<18}{'min':>10}{'median':>10}{'max':>10}{'%null':>10}")
    for col in interesting:
        if col not in df.columns:
            continue
        s = df[col]
        if s.notna().any():
            print(f"  {col:<18}{s.min():>10.2f}{s.median():>10.2f}"
                  f"{s.max():>10.2f}{s.isna().mean()*100:>9.1f}%")
        else:
            print(f"  {col:<18}{'(all null)':>40}")

    print("\n" + "=" * 64)
    print("CONFIGURATION (last observed value)")
    print("=" * 64)
    cfg_cols = [
        "temp_open_f", "temp_close_f", "rh_open_pct",
        "tank_heat_on_f", "tank_heat_off_f", "schedule_interval_ms",
        "eco_mode",
        "row1_start_pct", "row1_stop_pct", "row1_ms",
    ]
    for c in cfg_cols:
        if c in df.columns:
            print(f"  {c:<22} {df[c].iloc[-1]}")


def _prompt_selection(csvs: list[Path]) -> list[Path]:
    print(f"Found {len(csvs)} CSV file(s) in {csvs[0].parent}:\n")
    for i, p in enumerate(csvs, 1):
        size_kb = p.stat().st_size / 1024
        print(f"  [{i:>2}] {p.name}  ({size_kb:,.1f} KB)")
    print("\nSelect: 'all', a single number, or comma-separated numbers (e.g. 1,3,5)")
    raw = input("> ").strip().lower()

    if raw in ("all", "a", "*"):
        return csvs

    selected: list[Path] = []
    for tok in raw.split(","):
        tok = tok.strip()
        if not tok:
            continue
        try:
            idx = int(tok)
        except ValueError:
            print(f"  warn: '{tok}' is not a number, skipped", file=sys.stderr)
            continue
        if 1 <= idx <= len(csvs):
            selected.append(csvs[idx - 1])
        else:
            print(f"  warn: {idx} is out of range, skipped", file=sys.stderr)
    return selected


def write_report(csv_path: Path, out_dir: Path, stamp: str) -> Path:
    """Generate a .txt summary for one CSV. Returns the output file path."""
    df = load(csv_path)
    out = out_dir / f"{csv_path.stem}_summary_{stamp}.txt"
    with open(out, "w") as f, contextlib.redirect_stdout(f):
        print(f"Summary report")
        print(f"  source CSV : {csv_path.name}")
        print(f"  generated  : {dt.datetime.now().isoformat(timespec='seconds')}")
        print()
        summarize(df)
    return out


def main(argv: list[str]) -> int:
    cwd = Path.cwd()
    csvs = sorted(cwd.glob("*.csv"))
    if not csvs:
        print(f"No .csv files found in {cwd}", file=sys.stderr)
        return 1

    selected = _prompt_selection(csvs)
    if not selected:
        print("Nothing selected; exiting.", file=sys.stderr)
        return 1

    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    print(f"\nWriting {len(selected)} report(s)...")
    failures = 0
    for csv in selected:
        try:
            out = write_report(csv, cwd, stamp)
            print(f"  ok   {csv.name:<36}  →  {out.name}")
        except Exception as e:
            failures += 1
            print(f"  FAIL {csv.name:<36}  ({type(e).__name__}: {e})",
                  file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
