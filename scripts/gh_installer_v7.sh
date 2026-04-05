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

GH_USER="${SUDO_USER:-}"
GH_HOME=""

require_root
detect_user

GREENHOUSE_DIR="${GH_HOME}/greenhouse"
GREENHOUSE_DASHBOARD_DIR="${GREENHOUSE_DIR}/dashboard"
NR_USERDIR="${GH_HOME}/.node-red"

SCRIPT_PATH="$(readlink -f "$0")"
REPO_DIR="$(dirname "$(dirname "${SCRIPT_PATH}")")"

apt_base() {
  log "apt update + upgrade"
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

install_nodered() {
  log "Installing Node-RED (Node 22)"
  curl -fsSL https://raw.githubusercontent.com/node-red/linux-installers/master/deb/update-nodejs-and-nodered \
    | bash -s -- --confirm-root --confirm-install --node22
}

install_caddy() {
  log "Installing Caddy"

  apt install -y debian-keyring debian-archive-keyring apt-transport-https

  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' \
    | gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg

  curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' \
    | tee /etc/apt/sources.list.d/caddy-stable.list

  apt update
  apt install -y caddy

  log "Writing Caddyfile"

  cat <<EOF > /etc/caddy/Caddyfile
:80 {
    @root path /
    redir @root /index.html

    @nodered path /nodered*
    reverse_proxy @nodered localhost:1880

    reverse_proxy localhost:1880
}
EOF

  systemctl enable caddy
  systemctl restart caddy
}

configure_tailscale() {
  if command -v tailscale >/dev/null 2>&1; then
    log "Configuring Tailscale tag"
    tailscale up --advertise-tags=tag:greenhouse || true
  else
    log "Tailscale not installed — skipping tag setup"
  fi
}

configure_ufw() {
  log "Configuring UFW (port 80 only)"
  ufw allow OpenSSH
  ufw allow 80/tcp
  ufw --force enable
}

deploy_dashboard() {
  mkdir -p "${GREENHOUSE_DASHBOARD_DIR}"
  chown -R "${GH_USER}:${GH_USER}" "${GREENHOUSE_DIR}"

  if [[ -f "${REPO_DIR}/html/index.html" ]]; then
    cp -f "${REPO_DIR}/html/index.html" "${GREENHOUSE_DASHBOARD_DIR}/index.html"
    chown "${GH_USER}:${GH_USER}" "${GREENHOUSE_DASHBOARD_DIR}/index.html"
  fi
}

main() {
  log "User: ${GH_USER}"
  log "Home: ${GH_HOME}"

  apt_base
  install_os_deps
  install_nodered
  install_caddy
  configure_tailscale
  deploy_dashboard
  configure_ufw

  systemctl restart nodered.service || true

  log "DONE"
  echo "Access your system at: http://ghpi"
}

main
