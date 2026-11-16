#!/usr/bin/env bash
# PRTS terminal service - build, test and install on the Raspberry Pi.
#
# Run as the operator (no sudo needed):
#   bash scripts/deploy/deploy.sh [--skip-tests] [--no-hook]
#
# The login hook is appended to ~/.bashrc once; the marker makes the operation
# idempotent.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INSTALL_BIN_DIR="${HOME}/.local/bin"
USER_CONFIG_DIR="${HOME}/.config/prts"
HOOK_BEGIN="# >>> PRTS terminal boot sequence >>>"
HOOK_END="# <<< PRTS terminal boot sequence <<<"

RUN_TESTS=1
INSTALL_HOOK=1

for argument in "$@"; do
    case "$argument" in
        --skip-tests) RUN_TESTS=0 ;;
        --no-hook) INSTALL_HOOK=0 ;;
        -h|--help)
            sed -n '2,12p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            printf 'unknown argument: %s\n' "$argument" >&2
            exit 2
            ;;
    esac
done

log() {
    printf '[ deploy ] %s\n' "$*"
}

for tool in cmake g++ make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        log "missing build tool: $tool"
        log "run scripts/setup/provision_pi.sh first"
        exit 1
    fi
done

log "configuring $BUILD_DIR"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null

log "building"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

if [ "$RUN_TESTS" -eq 1 ]; then
    if command -v ctest >/dev/null 2>&1; then
        log "running unit tests"
        ctest --test-dir "$BUILD_DIR" --output-on-failure
    else
        log "ctest not available, running the test binary directly"
        "$BUILD_DIR/prts_tests"
    fi
fi

log "installing to $INSTALL_BIN_DIR"
install -d "$INSTALL_BIN_DIR" "$USER_CONFIG_DIR"
install -m 0755 "$BUILD_DIR/prts-boot" "$INSTALL_BIN_DIR/prts-boot"
install -m 0755 "$BUILD_DIR/prts-mqtt" "$INSTALL_BIN_DIR/prts-mqtt"
install -m 0755 "$BUILD_DIR/prts-camera" "$INSTALL_BIN_DIR/prts-camera"
install -m 0755 "$BUILD_DIR/prts-web" "$INSTALL_BIN_DIR/prts-web"
install -m 0755 "$PROJECT_ROOT/scripts/camera/prts-camera-web.sh" "$INSTALL_BIN_DIR/prts-camera-web"
install -m 0755 "$PROJECT_ROOT/stream/node/prts-stream.sh" "$INSTALL_BIN_DIR/prts-stream"

# The configuration schema evolves with the binary. A stale installed copy is
# backed up and replaced so that an old palette or pacing cannot silently
# survive an upgrade; [config] schema in the shipped file is the version marker.
readConfigSchema() {
    local file=$1
    if [ ! -f "$file" ]; then
        echo ""
        return
    fi
    sed -n 's/^[[:space:]]*schema[[:space:]]*=[[:space:]]*\([0-9][0-9]*\).*$/\1/p' "$file" | head -n 1
}

SHIPPED_SCHEMA="$(readConfigSchema "$PROJECT_ROOT/config/prts.conf")"
INSTALLED_SCHEMA="$(readConfigSchema "$USER_CONFIG_DIR/prts.conf")"

if [ ! -f "$USER_CONFIG_DIR/prts.conf" ] || [ "$SHIPPED_SCHEMA" != "$INSTALLED_SCHEMA" ]; then
    if [ -f "$USER_CONFIG_DIR/prts.conf" ]; then
        BACKUP="$USER_CONFIG_DIR/prts.conf.legacy.$(date +%Y%m%d%H%M%S)"
        cp -p "$USER_CONFIG_DIR/prts.conf" "$BACKUP"
        log "configuration schema $INSTALLED_SCHEMA -> $SHIPPED_SCHEMA, previous file kept at $BACKUP"
    fi
    install -m 0644 "$PROJECT_ROOT/config/prts.conf" "$USER_CONFIG_DIR/prts.conf"
    log "installed configuration at $USER_CONFIG_DIR/prts.conf (schema $SHIPPED_SCHEMA)"
else
    log "keeping existing configuration at $USER_CONFIG_DIR/prts.conf (schema $INSTALLED_SCHEMA)"
fi

if [ "$INSTALL_HOOK" -eq 1 ]; then
    BASHRC="${HOME}/.bashrc"
    touch "$BASHRC"
    if grep -qF "$HOOK_BEGIN" "$BASHRC"; then
        log "login hook already present in $BASHRC"
    else
        log "installing login hook into $BASHRC"
        {
            printf '\n%s\n' "$HOOK_BEGIN"
            printf '# Show the PRTS boot sequence once per interactive login.\n'
            printf 'if [ -n "$PS1" ] && [ -t 1 ] && [ -z "${PRTS_SKIP_BOOT:-}" ] \\\n'
            printf '   && [ -z "${PRTS_BOOTED:-}" ] && [ -x "$HOME/.local/bin/prts-boot" ]; then\n'
            printf '    export PRTS_BOOTED=1\n'
            printf '    "$HOME/.local/bin/prts-boot"\n'
            printf 'fi\n'
            printf '%s\n' "$HOOK_END"
        } >>"$BASHRC"
    fi
fi

log "smoke test (plain report)"
"$INSTALL_BIN_DIR/prts-boot" --check | tail -n 12

log "done - open a new ssh session to see the sequence"
