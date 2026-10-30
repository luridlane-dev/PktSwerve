#!/bin/bash
# Installs a downloaded PktSwerve update. PktSwerve starts this script as the
# logged-in user and quits; the script waits for it to exit, replaces the app
# bundle and opens it again. The old bundle is put back if the swap fails, and
# PktSwerve is opened again in every case.
#
#   apply-update.sh <app-pid> <dmg> <app-bundle> <log-file>
#
# The administrator password is asked (through the standard macOS dialog) only
# when the user cannot replace the bundle without it.
set -u

PID="${1:?app pid}"
DMG="${2:?dmg}"
APP="${3:?app bundle}"
LOG="${4:?log file}"

exec >>"$LOG" 2>&1
log() { printf '%s %s\n' "$(date '+%Y-%m-%dT%H:%M:%S')" "$*"; }

WORK="$(mktemp -d)"
MOUNT="$WORK/mnt"
cleanup() {
    hdiutil detach "$MOUNT" -quiet 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

reopen() { open "$APP" || log "Could not reopen $APP"; }
fail()   { log "FAILED: $*"; reopen; exit 1; }

log "Waiting for PktSwerve (pid $PID) to exit"
for _ in $(seq 1 40); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.5
done
if kill -0 "$PID" 2>/dev/null; then
    # The app asked to quit before starting this script, so whatever is left is only teardown.
    log "PktSwerve is still running after 20 s - ending it"
    kill -9 "$PID" 2>/dev/null
    sleep 1
    kill -0 "$PID" 2>/dev/null && fail "PktSwerve did not exit"
fi

mkdir -p "$MOUNT"
hdiutil attach "$DMG" -nobrowse -readonly -quiet -mountpoint "$MOUNT" || fail "could not open the disk image"
[ -d "$MOUNT/PktSwerve.app" ] || fail "PktSwerve.app is missing from the disk image"

APPS_DIR="$(dirname "$APP")"
NEW="$APPS_DIR/PktSwerve.new.app"
OLD="$APPS_DIR/PktSwerve.old.app"

# The privileged steps live in their own file so they can run either directly or
# through one administrator prompt.
STEPS="$WORK/steps.sh"
{
    echo 'set -u'
    printf 'rm -rf %q %q\n' "$NEW" "$OLD"
    printf 'cp -R %q %q || { rm -rf %q; exit 1; }\n' "$MOUNT/PktSwerve.app" "$NEW" "$NEW"
    printf 'xattr -dr com.apple.quarantine %q 2>/dev/null\n' "$NEW"
    # Ad-hoc signature made on this Mac, as install.sh does; Apple Silicon needs one.
    printf 'codesign --force --deep --sign - %q 2>/dev/null\n' "$NEW"
    printf 'mv %q %q || { rm -rf %q; exit 1; }\n' "$APP" "$OLD" "$NEW"
    printf 'if mv %q %q; then rm -rf %q; else mv %q %q; rm -rf %q; exit 1; fi\n' "$NEW" "$APP" "$OLD" "$OLD" "$APP" "$NEW"
} >"$STEPS"

if [ -w "$APPS_DIR" ] && [ -O "$APP" ]; then
    log "Replacing $APP"
    /bin/bash "$STEPS" || fail "could not replace the app"
else
    case "$WORK" in *[\"\\]*) fail "unsupported temporary path" ;; esac
    log "Replacing $APP (administrator rights needed)"
    osascript -e "do shell script \"/bin/bash '$STEPS'\" with administrator privileges" \
        || fail "could not replace the app (administrator password refused?)"
fi

log "Update installed"
reopen
