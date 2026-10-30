#!/bin/bash
# PktSwerve macOS installer. No Apple Developer account is involved: files
# fetched with curl carry no quarantine flag, so Gatekeeper never blocks them.
#
#   curl -fsSL https://raw.githubusercontent.com/luridlane-dev/PktSwerve/main/packaging/macos/install.sh | bash
#
# Options (pass after `bash -s --`):
#   --uninstall   remove the app and its login item
#   --dmg FILE    install from a local DMG instead of downloading
set -euo pipefail

DMG_URL="${PKTSWERVE_DMG_URL:-https://github.com/luridlane-dev/PktSwerve/releases/download/macos-latest/PktSwerve-macOS.dmg}"
APPS_DIR="${PKTSWERVE_APPS_DIR:-/Applications}"
APP="$APPS_DIR/PktSwerve.app"
AGENT="$HOME/Library/LaunchAgents/com.pktswerve.app.plist"

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || fail "This installer is for macOS only."

# Writes to the apps folder; asks for the admin password only if that is not allowed.
priv() { if [ -w "$APPS_DIR" ]; then "$@"; else sudo "$@"; fi; }

quit_app() {
    # The running copy is owned by root after the first Start, so it needs sudo.
    pkill -x PktSwerve 2>/dev/null || true
    if pgrep -x PktSwerve >/dev/null 2>&1; then sudo pkill -x PktSwerve || true; fi
}

if [ "${1:-}" = "--uninstall" ]; then
    say "Removing PktSwerve"
    quit_app
    priv rm -rf "$APP"
    rm -f "$AGENT"
    say "Done. Settings stay in ~/Library/Preferences and ~/Library/Application Support/PktSwerve."
    exit 0
fi

LOCAL_DMG=""
if [ "${1:-}" = "--dmg" ]; then
    LOCAL_DMG="${2:-}"
    [ -f "$LOCAL_DMG" ] || fail "DMG not found: $LOCAL_DMG"
fi

WORK="$(mktemp -d)"
MOUNT="$WORK/mnt"
cleanup() {
    hdiutil detach "$MOUNT" -quiet 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

if [ -n "$LOCAL_DMG" ]; then
    DMG="$LOCAL_DMG"
else
    DMG="$WORK/PktSwerve.dmg"
    say "Downloading PktSwerve"
    curl -fL --progress-bar -o "$DMG" "$DMG_URL" \
        || fail "Download failed. Check your internet connection and try again."
fi

say "Installing to $APPS_DIR"
mkdir -p "$MOUNT"
hdiutil attach "$DMG" -nobrowse -readonly -quiet -mountpoint "$MOUNT" \
    || fail "Could not open the disk image."
[ -d "$MOUNT/PktSwerve.app" ] || fail "PktSwerve.app is missing from the disk image."

quit_app
# Copy beside the old app first so a failed copy leaves it untouched.
priv rm -rf "$APPS_DIR/PktSwerve.new.app"
priv cp -R "$MOUNT/PktSwerve.app" "$APPS_DIR/PktSwerve.new.app"
priv xattr -dr com.apple.quarantine "$APPS_DIR/PktSwerve.new.app" 2>/dev/null || true
# Ad-hoc signature made on this Mac: free, and needed for Apple Silicon to run the binary.
priv codesign --force --deep --sign - "$APPS_DIR/PktSwerve.new.app" 2>/dev/null || true
priv rm -rf "$APP"
priv mv "$APPS_DIR/PktSwerve.new.app" "$APP"

say "Starting PktSwerve (look for its icon in the menu bar)"
open "$APP"
echo "Press Start in the popup; macOS asks for your password once to set up the network rules."
