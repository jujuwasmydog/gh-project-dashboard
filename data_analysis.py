#!/usr/bin/env python3

import sys
import pandas as pd

def load_data(csv_file):
    # Auto-detect delimiter
    df = pd.read_csv(csv_file, sep=None, engine="python")

    # Clean column names
    df.columns = [str(c).strip().replace("\r", "").replace("\n", "") for c in df.columns]

    print("Detected columns:", df.columns.tolist())

    if "timestamp" not in df.columns:
        raise ValueError(f"'timestamp' column not found. Found: {df.columns.tolist()}")

    # Parse timestamp
    df["timestamp"] = pd.to_datetime(df["timestamp"], errors="coerce", utc=True)

    # All non-timestamp columns are sensor columns
    sensor_cols = [c for c in df.columns if c != "timestamp"]

    for col in sensor_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # Drop invalid timestamps
    df = df.dropna(subset=["timestamp"]).copy()

    # Drop rows where all sensor fields are blank
    df = df.dropna(subset=sensor_cols, how="all").copy()

    # Sort by time
    df = df.sort_values("timestamp").reset_index(drop=True)

    return df

def print_averages(df):
    now = df["timestamp"].max()

    windows = {
        "Last 24 Hours": now - pd.Timedelta(hours=24),
        "Last Week": now - pd.Timedelta(days=7),
        "Last Month": now - pd.Timedelta(days=30),
        "All Time": None,
    }

    sensor_cols = [c for c in df.columns if c != "timestamp"]

    print(f"\nLatest timestamp in file: {now}")
    print("=" * 72)

    for col in sensor_cols:
        print(f"\n{col}")
        print("-" * 72)

        for label, start_time in windows.items():
            if start_time is None:
                subset = df[col]
            else:
                subset = df.loc[df["timestamp"] >= start_time, col]

            avg = subset.mean()

            if pd.isna(avg):
                print(f"{label:<15}: N/A")
            else:
                print(f"{label:<15}: {avg:.2f}")

def main():
    if len(sys.argv) != 2:
        print("Usage: python data_csv.py sensors.csv")
        sys.exit(1)

    csv_file = sys.argv[1]
    df = load_data(csv_file)

    if df.empty:
        print("No valid data found.")
        sys.exit(1)

    print_averages(df)

if __name__ == "__main__":
    main()
