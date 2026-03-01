#!/usr/bin/env bash
set -euo pipefail

log() { echo -e "\n[greenhouse] $*\n"; }

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

# Files expected in the repo directory you run from
REPO_FLOWS="flows.json"
REPO_CREDS="flows_cred.json"     # optional
REPO_DB_SCHEMA="gh_db_v2.sql"

# Node-RED nodes to install (NO unsafe-perm; correct RBE; includes serial)
NR_NODES=(
  "@flowfuse/node-red-dashboard"
  "node-red-node-sqlite"
  "node-red-node-ui-table"
  "node-red-node-serialport"
  "node-red-contrib-mqtt-broker"
  "node-red-contrib-time-range-switch"
  "node-red-node-rbe"
)

# Resolve repo dir based on script location (works regardless of current dir)
SCRIPT_PATH="$(readlink -f "$0")"
REPO_DIR="$(dirname "${SCRIPT_PATH}")"

# ----------------------------
# FUNCTIONS
# ----------------------------
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
    git unzip jq \
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

# CRITICAL: Override vendor unit defaults that run Node-RED as root and WD=/root
ensure_nodered_service_runs_as_user() {
  log "Configuring Node-RED systemd service (User=${GH_USER}, WD=${GH_HOME}, USERDIR=${NR_USERDIR})"

  mkdir -p "${NR_USERDIR}"
  chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

  mkdir -p /etc/systemd/system/nodered.service.d
  cat >/etc/systemd/system/nodered.service.d/override.conf <<EOF
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
EOF

  systemctl daemon-reload

  # Enable + start now (resilience)
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

enable_core_services() {
  log "Enabling core services (mosquitto, nodered)"

  # Force-enable at boot and start now
  systemctl enable --now mosquitto.service
  systemctl enable --now nodered.service
}

configure_ufw() {
  log "Configuring UFW: allow OpenSSH + allow 1880/tcp + enable"
  ufw allow OpenSSH
  ufw allow 1880/tcp
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

    log "Initializing Arduino CLI + installing AVR core"
    su - "${GH_USER}" -c "
      set -e
      arduino-cli config init || true
      arduino-cli core update-index
      arduino-cli core install arduino:avr
    "
  fi

  log "Adding ${GH_USER} to dialout for Arduino serial access"
  usermod -aG dialout "${GH_USER}" || true
}

deploy_repo_files_from_repo_dir() {
  log "Using repo directory: ${REPO_DIR}"
  log "Creating greenhouse dirs at ${GREENHOUSE_DIR}"
  mkdir -p "${GREENHOUSE_DIR}/db" "${GREENHOUSE_DIR}/logs"
  chown -R "${GH_USER}:${GH_USER}" "${GREENHOUSE_DIR}"

  log "Deploying flows + DB schema from repo -> Node-RED userdir / greenhouse db"

  systemctl stop nodered.service || true

  # DB schema -> greenhouse/db
  if [[ -f "${REPO_DIR}/${REPO_DB_SCHEMA}" ]]; then
    cp -f "${REPO_DIR}/${REPO_DB_SCHEMA}" "${GREENHOUSE_DIR}/db/"
    chown "${GH_USER}:${GH_USER}" "${GREENHOUSE_DIR}/db/${REPO_DB_SCHEMA}"
  else
    log "WARNING: Missing ${REPO_DIR}/${REPO_DB_SCHEMA} (skipping DB schema copy)"
  fi

  # flows -> ~/.node-red
  mkdir -p "${NR_USERDIR}"
  chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

  if [[ -f "${REPO_DIR}/${REPO_FLOWS}" ]]; then
    cp -f "${REPO_DIR}/${REPO_FLOWS}" "${NR_USERDIR}/flows.json"
    chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows.json"
  else
    log "WARNING: Missing ${REPO_DIR}/${REPO_FLOWS} (skipping flows.json copy)"
  fi

  # optional creds
  if [[ -f "${REPO_DIR}/${REPO_CREDS}" ]]; then
    cp -f "${REPO_DIR}/${REPO_CREDS}" "${NR_USERDIR}/flows_cred.json"
    chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows_cred.json"
  fi

  systemctl start nodered.service
}

apt_cleanup() {
  log "apt cleanup"
  apt-get autoremove -y
  apt-get autoclean -y
}

# ----------------------------
# MAIN
# ----------------------------
log "Target user: ${GH_USER}"
log "Home: ${GH_HOME}"
log "Installer: ${SCRIPT_PATH}"
log "Repo dir: ${REPO_DIR}"
log "Node-RED userdir: ${NR_USERDIR}"

apt_base
install_os_deps

install_nodered_node22
ensure_nodered_service_runs_as_user

install_nodered_nodes_as_user

install_arduino_cli

deploy_repo_files_from_repo_dir

enable_core_services
configure_ufw

systemctl restart nodered.service
systemctl --no-pager --full status nodered.service || true

apt_cleanup

log "DONE"
echo "Node-RED: http://<host>:1880"
echo "NOTE: ${GH_USER} was added to 'dialout' for Arduino uploads; log out/in (or reboot) for it to take effect."
