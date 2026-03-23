#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Searching for .ino files in ${REPO_ROOT}/Arduino and ${REPO_ROOT}/ATmega32..."

mapfile -t INOS < <(find "${REPO_ROOT}/Arduino" "${REPO_ROOT}/ATmega32" -type f -name "*.ino" 2>/dev/null | sort)

if [[ ${#INOS[@]} -eq 0 ]]; then
  echo "No .ino files found in Arduino/ or ATmega32/."
  exit 1
fi

echo
echo "Available sketches:"
select INO in "${INOS[@]}"; do
  [[ -n "${INO}" ]] && break
  echo "Invalid selection."
done

echo
echo "Selected: ${INO}"

echo
echo "Updating Arduino CLI index..."
arduino-cli core update-index

echo "Ensuring AVR core is installed..."
if ! arduino-cli core list | grep -q '^arduino:avr'; then
  arduino-cli core install arduino:avr
fi

echo "Detecting connected board..."
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1 || true)"

if [[ -z "${PORT}" ]]; then
  echo "No Arduino serial port detected."
  exit 1
fi

echo "Using serial port: ${PORT}"

echo
echo "Select target board:"
PS3="#? "
select BOARD in "Arduino Uno" "Arduino Mega 2560"; do
  case "${REPLY}" in
    1)
      FQBN="arduino:avr:uno"
      BOARD_LABEL="Uno"
      break
      ;;
    2)
      FQBN="arduino:avr:mega"
      BOARD_LABEL="Mega_2560"
      break
      ;;
    *)
      echo "Invalid selection."
      ;;
  esac
done

SKETCH_NAME="$(basename "${INO}" .ino)"
TEMP_NAME="${SKETCH_NAME}_${BOARD_LABEL}"
BUILD_DIR="/tmp/${TEMP_NAME}"

NODE_RED_STOPPED=0
NODE_RED_SERVICE_NAME=""
NODE_RED_SCOPE=""

run_systemctl() {
  local scope="$1"
  shift
  if [[ "${scope}" == "user" ]]; then
    systemctl --user "$@"
  else
    if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
      sudo systemctl "$@"
    else
      systemctl "$@"
    fi
  fi
}

service_exists() {
  local scope="$1"
  local service="$2"
  if [[ "${scope}" == "user" ]]; then
    systemctl --user list-unit-files "${service}.service" --no-legend 2>/dev/null | grep -q "^${service}\.service"
  else
    systemctl list-unit-files "${service}.service" --no-legend 2>/dev/null | grep -q "^${service}\.service"
  fi
}

service_is_active() {
  local scope="$1"
  local service="$2"
  if [[ "${scope}" == "user" ]]; then
    systemctl --user is-active --quiet "${service}.service"
  else
    if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
      sudo systemctl is-active --quiet "${service}.service"
    else
      systemctl is-active --quiet "${service}.service"
    fi
  fi
}

stop_node_red_if_running() {
  local candidates=("nodered" "node-red")
  local scopes=("system" "user")

  for scope in "${scopes[@]}"; do
    for service in "${candidates[@]}"; do
      if service_exists "${scope}" "${service}" && service_is_active "${scope}" "${service}"; then
        echo
        echo "Stopping Node-RED service: ${service}.service (${scope})..."
        run_systemctl "${scope}" stop "${service}.service"
        NODE_RED_STOPPED=1
        NODE_RED_SERVICE_NAME="${service}"
        NODE_RED_SCOPE="${scope}"
        sleep 2
        return 0
      fi
    done
  done

  echo
  echo "Node-RED service not active. Continuing..."
}

restart_node_red_if_needed() {
  if [[ "${NODE_RED_STOPPED}" -eq 1 ]]; then
    echo
    echo "Restarting Node-RED service: ${NODE_RED_SERVICE_NAME}.service (${NODE_RED_SCOPE})..."
    run_systemctl "${NODE_RED_SCOPE}" start "${NODE_RED_SERVICE_NAME}.service"
    sleep 2
    echo "Node-RED restarted."
  fi
}

cleanup() {
  restart_node_red_if_needed || true
}
trap cleanup EXIT

free_serial_port() {
  local port="$1"

  echo
  echo "Clearing any process still using ${port}..."
  if command -v fuser >/dev/null 2>&1; then
    fuser -k "${port}" >/dev/null 2>&1 || true
  elif command -v lsof >/dev/null 2>&1; then
    lsof -t "${port}" 2>/dev/null | xargs -r kill -9 || true
  fi
  sleep 2
}

stop_node_red_if_running
free_serial_port "${PORT}"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cp "${INO}" "${BUILD_DIR}/${TEMP_NAME}.ino"

echo
echo "Compiling ${INO} for ${BOARD_LABEL//_/ }..."
arduino-cli compile --fqbn "${FQBN}" "${BUILD_DIR}"

echo
echo "Uploading to ${PORT}..."
arduino-cli upload -p "${PORT}" --fqbn "${FQBN}" "${BUILD_DIR}"

echo
echo "Upload complete."
