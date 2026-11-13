#!/usr/bin/env bash
# PRTS terminal service - Raspberry Pi environment provisioning.
#
# Run as root:   echo <password> | sudo -S bash scripts/setup/provision_pi.sh --user seven
#
# The script is idempotent: re-running it only installs what is missing.
set -euo pipefail

TARGET_USER="${SUDO_USER:-seven}"
INSTALL_KEY=""
LOCALE_NAME="C.UTF-8"

usage() {
    cat <<'USAGE'
Usage: sudo bash provision_pi.sh [options]

Options:
  --user NAME        operator account to configure (default: SUDO_USER or seven)
  --ssh-key PATH     install this public key into the operator's authorized_keys
  --locale NAME      locale to export for interactive shells (default: C.UTF-8)
  -h, --help         show this help
USAGE
}

log() {
    printf '[ provision ] %s\n' "$*"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --user)
            TARGET_USER="${2:?--user requires a value}"
            shift 2
            ;;
        --ssh-key)
            INSTALL_KEY="${2:?--ssh-key requires a path}"
            shift 2
            ;;
        --locale)
            LOCALE_NAME="${2:?--locale requires a value}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    log "this script must run as root (use sudo)"
    exit 1
fi

if ! id "$TARGET_USER" >/dev/null 2>&1; then
    log "user $TARGET_USER does not exist"
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive

log "updating package index"
apt-get update -qq

PACKAGES=(
    build-essential
    cmake
    git
    gdb
    vim
    tmux
    htop
    tree
    rsync
    curl
    locales
    python3
    device-tree-compiler
    v4l-utils
    libjpeg62-turbo-dev
    mosquitto
    mosquitto-clients
)

log "installing toolchain: ${PACKAGES[*]}"
apt-get install -y -qq --no-install-recommends "${PACKAGES[@]}" >/dev/null

log "ensuring locale $LOCALE_NAME exists"
if ! locale -a 2>/dev/null | grep -qi "^${LOCALE_NAME%%[.@]*}\.utf8$"; then
    if command -v locale-gen >/dev/null 2>&1 && [ -f /etc/locale.gen ]; then
        grep -qi "^${LOCALE_NAME%%[.@]*}\.UTF-8 UTF-8" /etc/locale.gen ||
            printf '%s UTF-8\n' "${LOCALE_NAME%%[.@]*}" >>/etc/locale.gen
        locale-gen >/dev/null
    fi
fi

USER_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"

log "creating state and configuration directories"
install -d -o "$TARGET_USER" -g "$TARGET_USER" -m 0755 \
    "$USER_HOME/.local/bin" \
    "$USER_HOME/.local/state/prts" \
    "$USER_HOME/.config/prts"

if [ -n "$INSTALL_KEY" ]; then
    if [ -f "$INSTALL_KEY" ]; then
        log "installing public key for $TARGET_USER"
        install -d -o "$TARGET_USER" -g "$TARGET_USER" -m 0700 "$USER_HOME/.ssh"
        touch "$USER_HOME/.ssh/authorized_keys"
        chown "$TARGET_USER:$TARGET_USER" "$USER_HOME/.ssh/authorized_keys"
        chmod 0600 "$USER_HOME/.ssh/authorized_keys"
        KEY_CONTENT="$(tr -d '\r\n' <"$INSTALL_KEY")"
        if ! grep -qF "$KEY_CONTENT" "$USER_HOME/.ssh/authorized_keys"; then
            printf '%s\n' "$KEY_CONTENT" >>"$USER_HOME/.ssh/authorized_keys"
        fi
    else
        log "warning: key file $INSTALL_KEY not found, skipping"
    fi
fi

log "exporting locale for interactive shells"
PROFILE_SNIPPET="$USER_HOME/.profile"
if ! grep -q "PRTS locale" "$PROFILE_SNIPPET" 2>/dev/null; then
    {
        printf '\n# PRTS locale\n'
        printf 'export LANG=%s\n' "$LOCALE_NAME"
        printf 'export LC_ALL=%s\n' "$LOCALE_NAME"
    } >>"$PROFILE_SNIPPET"
    chown "$TARGET_USER:$TARGET_USER" "$PROFILE_SNIPPET"
fi

log "toolchain versions"
{
    printf '  gcc   : %s\n' "$(gcc --version | head -n1)"
    printf '  g++   : %s\n' "$(g++ --version | head -n1)"
    printf '  cmake : %s\n' "$(cmake --version | head -n1)"
    printf '  git   : %s\n' "$(git --version)"
    printf '  gdb   : %s\n' "$(gdb --version | head -n1)"
    printf '  python: %s\n' "$(python3 --version)"
} | sed 's/^/  /'

log "kernel headers"
if [ -d "/lib/modules/$(uname -r)/build" ]; then
    log "  headers present for $(uname -r)"
else
    log "  headers missing for $(uname -r) - install linux-headers-$(uname -r) before building kernel modules"
fi

log "provisioning complete"
