#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Greenhouse bootstrap script
# - apt update/upgrade
# - install OS deps
# - install Node.js LTS + Node-RED + Dashboard nodes
# - install Arduino CLI + AVR core
# - enable UFW + OpenSSH
# - clone/update GitHub repo + deploy flows + DB schema
# ============================================================

log() { echo -e "\n[greenhouse] $*\n"; }

require_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "Run as root: sudo $0"
    exit 1
  fi
}

detect_user() {
  # Prefer the user who invoked sudo
  if [[ -n "${SUDO_USER-}" && "${SUDO_USER}" != "root" ]]; then
    GH_USER="${SUDO_USER}"
  else
    GH_USER="$(awk -F: '$3>=1000 && $1!="nobody" {print $1; exit}' /etc/passwd)"
    GH_USER="${GH_USER:-pi}"
  fi
  GH_HOME="$(eval echo "~${GH_USER}")"
}

clone_or_update_repo() {
  local repo_parent="${GH_HOME}/${REPO_DIRNAME}"
  local repo_path="${repo_parent}/${REPO_NAME}"

  mkdir -p "${repo_parent}"
  chown -R "${GH_USER}:${GH_USER}" "${repo_parent}"

  if [[ -d "${repo_path}/.git" ]]; then
    log "Updating existing repo at ${repo_path}"
    su - "${GH_USER}" -c "
      set -e
      cd '${repo_path}'
      git fetch --all --prune
      git checkout '${REPO_BRANCH}'
      git pull --ff-only origin '${REPO_BRANCH}'
    "
  else
    log "Cloning repo to ${repo_path}"
    su - "${GH_USER}" -c "
      set -e
      cd '${repo_parent}'
      git clone --branch '${REPO_BRANCH}' '${REPO_URL}' '${REPO_NAME}'
    "
  fi

  echo "${repo_path}"
}

install_arduino_cli() {
  if command -v arduino-cli >/dev/null 2>&1; then
    log "Arduino CLI already installed: $(arduino-cli version 2>/dev/null || true)"
    return 0
  fi

  log "Installing Arduino CLI"
  # Official installer drops into ./bin by default
  su - "${GH_USER}" -c "
    set -e
    cd '${GH_HOME}'
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | bash
  "

  # Move binary to global PATH
  install -m 755 "${GH_HOME}/bin/arduino-cli" /usr/local/bin/arduino-cli
  rm -rf "${GH_HOME}/bin"

  log "Initializing Arduino CLI and installing AVR core"
  su - "${GH_USER}" -c "
    set -e
    arduino-cli config init || true
    arduino-cli core update-index
    arduino-cli core install arduino:avr
  "

  # Allow non-root serial uploads (common for Uno/Nano/Mega on /dev/ttyACM0 or /dev/ttyUSB0)
  log "Adding ${GH_USER} to dialout group for USB serial access"
  usermod -aG dialout "${GH_USER}" || true
}

# ----------------------------
# CONFIG (edit if needed)
# ----------------------------
REPO_URL="https://github.com/jujuwasmydog/gh-project-dashboard.git"
REPO_BRANCH="main"
REPO_DIRNAME="git_hub"
REPO_NAME="gh-project-dashboard"

GREENHOUSE_DIRNAME="greenhouse"

# Repo file locations (current assumption: repo root)
REPO_FLOWS_PATH="flows.json"
REPO_CREDS_PATH="flows_cred.json"   # optional
REPO_DB_SCHEMA_PATH="gh_db_v2.sql"

# ----------------------------
# Main
# ----------------------------
require_root
detect_user

log "Target user: ${GH_USER} (home: ${GH_HOME})"

export DEBIAN_FRONTEND=noninteractive

# Update + upgrade
log "apt update + full-upgrade"
apt-get update -y
apt-get full-upgrade -y

# OS deps (NOTE: npm is NOT installed via apt; NodeSource nodejs includes npm)
log "Installing OS dependencies"
apt-get install -y \
  ca-certificates curl gnupg lsb-release \
  build-essential python3 make g++ \
  git unzip jq \
  sqlite3 \
  mosquitto mosquitto-clients \
  ufw \
  openssh-server

# Node.js LTS (NodeSource)
if ! command -v node >/dev/null 2>&1; then
  log "Installing Node.js LTS (NodeSource)"
  curl -fsSL https://deb.nodesource.com/setup_lts.x | bash -
  apt-get install -y nodejs
else
  log "Node already installed: $(node -v)"
fi

# Node-RED
if ! command -v node-red >/dev/null 2>&1; then
  log "Installing Node-RED"
  curl -fsSL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered \
    | bash -s -- --confirm-root --confirm-install
else
  log "Node-RED already installed"
fi

# Enable Node-RED service
log "Enabling Node-RED systemd service"
systemctl enable nodered.service
systemctl restart nodered.service || true

# Ensure Node-RED user dir exists and is owned by GH_USER
NR_USERDIR="${GH_HOME}/.node-red"
mkdir -p "${NR_USERDIR}"
chown -R "${GH_USER}:${GH_USER}" "${NR_USERDIR}"

# Install Node-RED dashboard/nodes (as GH_USER)
log "Installing Node-RED nodes (Dashboard 2 + helpers)"
su - "${GH_USER}" -c "
  set -e
  cd '${NR_USERDIR}'
  npm install --unsafe-perm --no-update-notifier --no-fund --no-audit \
    @flowfuse/node-red-dashboard \
    node-red-node-sqlite \
    node-red-node-ui-table \
    node-red-contrib-mqtt-broker \
    node-red-contrib-time-range-switch \
    node-red-contrib-rbe
"

# Firewall: allow SSH and enable UFW
log "Configuring firewall (UFW) to allow SSH"
ufw allow OpenSSH
ufw --force enable

# Arduino CLI
install_arduino_cli

# Clone/pull GitHub repo
REPO_PATH="$(clone_or_update_repo)"

# Create greenhouse directories
GREENHOUSE_ROOT="${GH_HOME}/${GREENHOUSE_DIRNAME}"
log "Creating greenhouse directories at ${GREENHOUSE_ROOT}"
mkdir -p "${GREENHOUSE_ROOT}/db" "${GREENHOUSE_ROOT}/logs"
chown -R "${GH_USER}:${GH_USER}" "${GREENHOUSE_ROOT}"

# Deploy flows + DB schema from repo
log "Deploying flows + DB schema from repo"

# Stop Node-RED before copying flows/settings to avoid race conditions
systemctl stop nodered.service || true

# Copy DB schema
if [[ -f "${REPO_PATH}/${REPO_DB_SCHEMA_PATH}" ]]; then
  cp -f "${REPO_PATH}/${REPO_DB_SCHEMA_PATH}" "${GREENHOUSE_ROOT}/db/"
  chown "${GH_USER}:${GH_USER}" "${GREENHOUSE_ROOT}/db/$(basename "${REPO_DB_SCHEMA_PATH}")"
else
  log "WARNING: ${REPO_PATH}/${REPO_DB_SCHEMA_PATH} not found (skipping DB schema copy)"
fi

# Copy flows.json
if [[ -f "${REPO_PATH}/${REPO_FLOWS_PATH}" ]]; then
  cp -f "${REPO_PATH}/${REPO_FLOWS_PATH}" "${NR_USERDIR}/flows.json"
  chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows.json"
else
  log "WARNING: ${REPO_PATH}/${REPO_FLOWS_PATH} not found (skipping flows.json copy)"
fi

# Copy flows_cred.json (optional)
if [[ -f "${REPO_PATH}/${REPO_CREDS_PATH}" ]]; then
  cp -f "${REPO_PATH}/${REPO_CREDS_PATH}" "${NR_USERDIR}/flows_cred.json"
  chown "${GH_USER}:${GH_USER}" "${NR_USERDIR}/flows_cred.json"
fi

# Start Node-RED again
systemctl start nodered.service
systemctl --no-pager --full status nodered.service || true

# Cleanup
log "Apt cleanup"
apt-get autoremove -y
apt-get autoclean -y

log "DONE"
echo "Node-RED:  http://<host>:1880"
echo "NOTE: ${GH_USER} was added to 'dialout' for Arduino uploads; log out/in for it to take effect."