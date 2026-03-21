#!/usr/bin/env bash
set -euo pipefail

echo "Looking for .ino files in $(pwd)"

mapfile -t INOS < <(find . -maxdepth 1 -type f -name "*.ino" -printf "%f\n" | sort)
if [[ ${#INOS[@]} -eq 0 ]]; then
  echo "No .ino files found."
  exit 1
fi

echo "Available sketches:"
select INO in "${INOS[@]}"; do
  [[ -n "${INO}" ]] && break
  echo "Invalid selection."
done

echo
echo "Updating Arduino CLI index..."
arduino-cli core update-index

echo "Ensuring AVR core is installed..."
if ! arduino-cli core list | grep -q '^arduino:avr'; then
  arduino-cli core install arduino:avr
fi

echo "Detecting connected board..."
BOARD_LIST="$(arduino-cli board list)"
echo "${BOARD_LIST}"

PORT="$(echo "${BOARD_LIST}" | awk '/ttyACM|ttyUSB/ {print $1; exit}')"
AUTO_FQBN="$(echo "${BOARD_LIST}" | awk '/ttyACM|ttyUSB/ {print $NF; exit}')"

if [[ -z "${PORT}" ]]; then
  echo "No Arduino serial port detected."
  exit 1
fi

case "${AUTO_FQBN}" in
  arduino:avr:uno)
    FQBN="arduino:avr:uno"
    BOARD_LABEL="Uno"
    ;;
  arduino:avr:mega|arduino:avr:megaADK)
    FQBN="arduino:avr:mega"
    BOARD_LABEL="Mega_2560"
    ;;
  *)
    echo
    echo "Could not confidently identify board type."
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
    ;;
esac

SKETCH_NAME="${INO%.ino}"
TEMP_NAME="${SKETCH_NAME}_${BOARD_LABEL}"
BUILD_DIR="/tmp/${TEMP_NAME}"

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Arduino CLI requires the main .ino filename to match the folder name
cp "${INO}" "${BUILD_DIR}/${TEMP_NAME}.ino"

echo
echo "Compiling ${INO} for ${BOARD_LABEL//_/ }..."
arduino-cli compile --fqbn "${FQBN}" "${BUILD_DIR}"

echo "Uploading to ${PORT}..."
arduino-cli upload -p "${PORT}" --fqbn "${FQBN}" "${BUILD_DIR}"

echo "Upload complete."

