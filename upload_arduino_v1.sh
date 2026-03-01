#!/usr/bin/env bash
set -euo pipefail

echo "Looking for .ino files in $(pwd)"

# Find .ino files in repo root
mapfile -t INOS < <(ls *.ino 2>/dev/null || true)
if [[ ${#INOS[@]} -eq 0 ]]; then
  echo "No .ino files found."
  exit 1
fi

echo "Available sketches:"
select INO in "${INOS[@]}"; do
  [[ -n "${INO}" ]] && break
done

SKETCH_NAME="${INO%.ino}"
BUILD_DIR="/tmp/${SKETCH_NAME}"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cp "${INO}" "${BUILD_DIR}/${SKETCH_NAME}.ino"

echo "Compiling..."
arduino-cli compile --fqbn arduino:avr:uno "${BUILD_DIR}"

echo "Detecting Arduino port..."
PORT="$(arduino-cli board list | awk '/ttyACM|ttyUSB/ {print $1; exit}')"
if [[ -z "${PORT}" ]]; then
  echo "No Arduino detected. Plug it in and try again."
  exit 1
fi

echo "Uploading to ${PORT}..."

# If Node-RED is holding the port, stop it temporarily
NEED_RESTART=0
if sudo lsof "${PORT}" >/dev/null 2>&1; then
  echo "Port ${PORT} is busy. Checking if Node-RED is using it..."
  if sudo lsof "${PORT}" 2>/dev/null | awk '{print $1}' | grep -qiE 'node|node-red'; then
    echo "Node-RED appears to be using ${PORT}. Stopping Node-RED for upload..."
    sudo systemctl stop nodered
    NEED_RESTART=1
    # Give the OS a moment to release the device
    sleep 2
  else
    echo "WARNING: ${PORT} is busy by another process. Upload may fail."
  fi
fi

# Ensure Node-RED restarts even if upload fails
cleanup() {
  if [[ "${NEED_RESTART}" -eq 1 ]]; then
    echo "Restarting Node-RED..."
    sudo systemctl start nodered
  fi
}
trap cleanup EXIT

arduino-cli upload -p "${PORT}" --fqbn arduino:avr:uno "${BUILD_DIR}"

echo "Upload complete."