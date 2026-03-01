#!/usr/bin/env bash
set -euo pipefail

# Must run from inside repo directory
REPO_DIR="$(pwd)"

echo "Looking for .ino files in $REPO_DIR"

mapfile -t INOS < <(ls *.ino 2>/dev/null || true)

if [ ${#INOS[@]} -eq 0 ]; then
  echo "No .ino files found."
  exit 1
fi

echo "Available sketches:"
select INO in "${INOS[@]}"; do
  if [ -n "$INO" ]; then
    break
  fi
done

SKETCH_NAME="${INO%.ino}"
BUILD_DIR="/tmp/${SKETCH_NAME}"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cp "$INO" "$BUILD_DIR/${SKETCH_NAME}.ino"

echo "Compiling..."
arduino-cli compile --fqbn arduino:avr:uno "$BUILD_DIR"

echo "Detecting Arduino port..."
PORT=$(arduino-cli board list | awk '/ttyACM|ttyUSB/ {print $1; exit}')

if [ -z "$PORT" ]; then
  echo "No Arduino detected."
  exit 1
fi

echo "Uploading to $PORT..."
arduino-cli upload -p "$PORT" --fqbn arduino:avr:uno "$BUILD_DIR"

echo "Upload complete."