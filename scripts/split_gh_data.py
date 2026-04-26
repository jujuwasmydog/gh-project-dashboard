#!/usr/bin/env python3
"""Split gh_data.csv into one CSV per UTC day, each with a header row.

Output: <data_dir>/daily/gh_data_YYYY-MM-DD.csv

Reuses load() from load_gh_data.py so the embedded header rows and the
mostly-empty raw_json column are stripped before splitting.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pandas as pd

from load_gh_data import DEFAULT_CSV, load


def split_per_day(src: Path = DEFAULT_CSV, out_dir: Path | None = None) -> list[Path]:
    df = load(src)
    out_dir = out_dir or src.parent / "daily"
    out_dir.mkdir(exist_ok=True)

    written: list[Path] = []
    for day, chunk in df.groupby(df.index.date):
        out_df = chunk.reset_index()
        # Match the original format: ts as integer milliseconds since epoch.
        # Compute via timedelta arithmetic so we don't depend on the
        # underlying datetime resolution (ns vs ms) or tz handling.
        epoch = pd.Timestamp("1970-01-01", tz="UTC")
        out_df["ts"] = (out_df["ts"] - epoch) // pd.Timedelta("1ms")
        out = out_dir / f"gh_data_{day.isoformat()}.csv"
        out_df.to_csv(out, index=False)
        written.append(out)
        print(f"  {out.name}  rows={len(out_df):>6,}  "
              f"size={out.stat().st_size/1024:>7.1f} KB")
    return written


def main(argv: list[str]) -> int:
    src = Path(argv[1]) if len(argv) > 1 else DEFAULT_CSV
    if not src.exists():
        print(f"error: {src} not found", file=sys.stderr)
        return 1
    print(f"Splitting {src.name} per UTC day...")
    files = split_per_day(src)
    print(f"\nWrote {len(files)} files to {files[0].parent}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
