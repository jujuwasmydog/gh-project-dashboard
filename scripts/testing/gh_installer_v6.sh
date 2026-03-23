#!/usr/bin/env bash
set -euo pipefail

log() { echo -e "\n[greenhouse] $*\n"; }
warn() { echo -e "\n[greenhouse][WARNING] $*\n"; }
die() { echo -e "\n[greenhouse][ERROR] $*\n"; exit 1; }

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "Run as root: sudo $0"
    exit 1
  fi
}

detect_user() {
  if [[ -n "${SUDO_USER-}" && "${SUDO_USER}" != "root" ]]; then
    GH_USER="${SUDO_USER}"
  else
    GH_USER="$(awk -F: '$3>=1000 && $1!="nobody" {print $1; exit}' /etc/passwd || true)"
    GH_USER="${GH_USER:-pi}"
  fi
  GH_HOME="$(eval echo "~${GH_USER}")"
}

# Safe init for set -u
GH_USER="${SUDO_USER:-}"
GH_HOME=""

require_root
detect_user

# ----------------------------
# CONFIG
# ----------------------------
GREENHOUSE_DIR="${GH_HOME}/greenhouse"
NR_USERDIR="${GH_HOME}/.node-red"
DB_DIR="${GREENHOUSE_DIR}/db"
LOG_DIR="${GREENHOUSE_DIR}/logs"
DB_FILE="${DB_DIR}/greenhouse_db_v2.db"

NR_NODES=(
  "@flowfuse/node-red-dashboard"
  "node-red-node-sqlite"
  "node-red-node-ui-table"
  "node-red-node-serialport"
  "node-red-contrib-mqtt-broker"
  "node-red-contrib-time-range-switch"
  "node-red-node-rbe"
)

SCRIPT_PATH="$(readlink -f "$0")"
SCRIPT_DIR="$(dirname "${SCRIPT_PATH}")"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

REPO_FLOWS="${REPO_ROOT}/node-red/flows.json"
REPO_CREDS="${REPO_ROOT}/node-red/flows_cred.json"   # optional
REPO_DB_SCHEMA="${REPO_ROOT}/Database/gh_db_v3.sql"
REPO_ARDUINO_DIR="${REPO_ROOT}/Arduino"
REPO_ATMEGA32_DIR="${REPO_ROOT}/ATmega32"

# ----------------------------
# FUNCTIONS
# ----------------------------
print_header() {
  log "Target user: ${GH_USER}"
  log "Home: ${GH_HOME}"
  log "Installer: ${SCRIPT_PATH}"
  log "Script dir: ${SCRIPT_DIR}"
  log "Repo root: ${REPO_ROOT}"
  log "Node-RED userdir: ${NR_USERDIR}"
}

validate_repo_layout() {
  log "Validating repo architecture"

  [[ -d "${REPO_ROOT}" ]] || die "Repo root not found: ${REPO_ROOT}"
  [[ -d "${SCRIPT_DIR}" ]] || die "Script directory not found: ${SCRIPT_DIR}"
  [[ -d "${REPO_ROOT}/scripts" ]] || die "Missing scripts/ directory in repo root"

  [[ -f "${REPO_FLOWS}" ]] || die "Missing required flows file: ${REPO_FLOWS}"
  [[ -f "${REPO_DB_SCHEMA}" ]] || die "Missing required DB schema: ${REPO_DB_SCHEMA}"

  if [[ ! -d "${REPO_ARDUINO_DIR}" ]]; then
    warn "Missing Arduino directory: ${REPO_ARDUINO_DIR}"
  fi

  if [[ ! -d "${REPO_ATMEGA32_DIR}" ]]; then
    warn "Missing ATmega32 directory: ${REPO_ATMEGA32_DIR}"
  fi

  if ! compgen -G "${REPO_ARDUINO_DIR}/*.ino" >/dev/null 2>&1; then
    warn "No .ino files found in ${REPO_ARDUINO_DIR}"
  fi

  if ! compgen -G "${REPO_ATMEGA32_DIR}/*.ino" >/dev/null 2>&1; then
    warn "No .ino files found in ${REPO_ATMEGA32_DIR}"
  fi
}

apt_base() {
  log "apt update + full-upgrade"
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  apt-get full-upgrade -y
}

install_os_deps() {
  log "Installing OS dependencies"
  apt-get install -y \
    ca-certificates curl gnupg lsb-release \
    build-essential python3 make g++ \
    git unzip jq tree \
    sqlite3 \
    mosquitto mosquitto-clients \
    ufw \
    openssh-server
}

install_nodered_node22() {
  log "Installing Node-RED (with Node 22 LTS)"
  curl -fsSL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered \
    | bash -s -- --confirm-root --confirm-install --node22
}

ensure_nodered_service_runs_as_user() {
  log "Configuring Node-RED systemd service (User=${GH_USER}, WD=${GH_HOME}, USERDIR=${NR_USERDIR})"

  mkdir -p "${NR_USERDIR}"
  chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

  mkdir -p /etc/systemd/system/nodered.service.d
  cat >/etc/systemd/system/nodered.service.d/override.conf <<EONR
[Unit]
After=network-online.target
Wants=network-online.target

[Service]
User=${GH_USER}
Group=${GH_USER}
WorkingDirectory=${GH_HOME}
Environment="NODE_RED_USER_DIR=${NR_USERDIR}"
EnvironmentFile=-${NR_USERDIR}/environment
Environment="NODE_RED_OPTIONS="
Restart=on-failure
RestartSec=10
EONR

  systemctl daemon-reload
  systemctl enable --now nodered.service
}

install_nodered_nodes_as_user() {
  log "Installing Node-RED nodes into ${NR_USERDIR} (as ${GH_USER})"

  systemctl stop nodered.service || true

  mkdir -p "${NR_USERDIR}"
  chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

  su - "${GH_USER}" -c "
    set -e
    cd '${NR_USERDIR}'
    npm install --no-update-notifier --no-fund --no-audit ${NR_NODES[*]}
  "

  systemctl start nodered.service
}

verify_npm_install() {
  log "Verifying Node-RED npm package installation"

  su - "${GH_USER}" -c "
    set -e
    cd '${NR_USERDIR}'
    npm list --depth=0 || true
  "

  local missing=0
  for node_pkg in "${NR_NODES[@]}"; do
    if [[ ! -d "${NR_USERDIR}/node_modules/${node_pkg}" ]]; then
      warn "Missing Node-RED package after npm install: ${node_pkg}"
      missing=1
    fi
  done

  if [[ "${missing}" -ne 0 ]]; then
    die "One or more required Node-RED npm packages are missing"
  fi
}

enable_core_services() {
  log "Enabling core services (mosquitto, nodered)"
  systemctl enable --now mosquitto.service
  systemctl enable --now nodered.service
}

configure_ufw() {
  log "Configuring UFW: allow OpenSSH + allow 1880/tcp + enable"
  ufw allow OpenSSH || true
  ufw allow 1880/tcp || true
  ufw --force enable
}

install_arduino_cli() {
  if command -v arduino-cli >/dev/null 2>&1; then
    log "Arduino CLI already installed: $(arduino-cli version 2>/dev/null || true)"
  else
    log "Installing Arduino CLI"
    su - "${GH_USER}" -c "
      set -e
      cd '${GH_HOME}'
      curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | bash
    "

    install -m 755 "${GH_HOME}/bin/arduino-cli" /usr/local/bin/arduino-cli
    rm -rf "${GH_HOME}/bin"
  fi

  log "Initializing Arduino CLI + installing AVR core"
  su - "${GH_USER}" -c "
    set -e
    arduino-cli config init || true
    arduino-cli core update-index
    arduino-cli core install arduino:avr
  "

  log "Adding ${GH_USER} to dialout for Arduino serial access"
  usermod -aG dialout "${GH_USER}" || true
}

verify_arduino_cli() {
  log "Verifying Arduino CLI installation and AVR core"

  command -v arduino-cli >/dev/null 2>&1 || die "arduino-cli is not available in PATH"
  arduino-cli version || true

  if ! su - "${GH_USER}" -c "arduino-cli core list | grep -q '^arduino:avr'"; then
    die "Arduino AVR core not installed correctly"
  fi

  su - "${GH_USER}" -c "arduino-cli core list" || true
}

prepare_greenhouse_dirs() {
  log "Creating greenhouse directories"
  mkdir -p "${DB_DIR}" "${LOG_DIR}"
  chown -R "${GH_USER}:${GH_USER}" "${GREENHOUSE_DIR}"
}

deploy_repo_files() {
  log "Deploying repo files to target locations"

  systemctl stop nodered.service || true

  mkdir -p "${NR_USERDIR}"
  chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

  cp -f "${REPO_FLOWS}" "${NR_USERDIR}/flows.json"
  chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows.json"

  if [[ -f "${REPO_CREDS}" ]]; then
    cp -f "${REPO_CREDS}" "${NR_USERDIR}/flows_cred.json"
    chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows_cred.json"
  fi

  cp -f "${REPO_DB_SCHEMA}" "${DB_DIR}/gh_db_v3.sql"
  chown "${GH_USER}:${GH_USER}" "${DB_DIR}/gh_db_v3.sql"

  rm -f "${DB_FILE}"
  sqlite3 "${DB_FILE}" < "${REPO_DB_SCHEMA}"
  chown "${GH_USER}:${GH_USER}" "${DB_FILE}"

  systemctl start nodered.service
}

check_serial_devices() {
  log "Checking serial devices"

  local found=0
  for pattern in /dev/ttyACM* /dev/ttyUSB*; do
    if compgen -G "${pattern}" >/dev/null 2>&1; then
      ls -l ${pattern} || true
      found=1
    fi
  done

  if [[ "${found}" -eq 0 ]]; then
    warn "No /dev/ttyACM* or /dev/ttyUSB* devices detected"
  fi
}

show_service_status() {
  log "Service status: nodered"
  systemctl --no-pager --full status nodered.service || true

  log "Service status: mosquitto"
  systemctl --no-pager --full status mosquitto.service || true
}

print_repo_tree() {
  log "Repo tree (${REPO_ROOT})"
  tree -L 3 "${REPO_ROOT}" || true
}

apt_cleanup() {
  log "apt cleanup"
  apt-get autoremove -y
  apt-get autoclean -y
}

# ----------------------------
# MAIN
# ----------------------------
print_header
validate_repo_layout
apt_base
install_os_deps
install_nodered_node22
ensure_nodered_service_runs_as_user
install_nodered_nodes_as_user
verify_npm_install
install_arduino_cli
verify_arduino_cli
prepare_greenhouse_dirs
deploy_repo_files
enable_core_services
configure_ufw
check_serial_devices
show_service_status
print_repo_tree
apt_cleanup

log "DONE"
echo "Node-RED: http://<host>:1880"
echo "NOTE: ${GH_USER} was added to 'dialout' for Arduino uploads; log out/in (or reboot) for it to take effect."
