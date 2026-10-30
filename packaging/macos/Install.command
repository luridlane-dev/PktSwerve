#!/bin/bash
# Double-click installer inside the DMG: copies PktSwerve to /Applications and
# clears the download quarantine flag.
cd "$(dirname "$0")" || exit 1

set -e
APPS_DIR="${PKTSWERVE_APPS_DIR:-/Applications}"
APP="$APPS_DIR/PktSwerve.app"
[ -d PktSwerve.app ] || { echo "PktSwerve.app must be in the same folder as this script."; exit 1; }

priv() { if [ -w "$APPS_DIR" ]; then "$@"; else sudo "$@"; fi; }

pkill -x PktSwerve 2>/dev/null || true
if pgrep -x PktSwerve >/dev/null 2>&1; then sudo pkill -x PktSwerve || true; fi
# Copy beside the old app first so a failed copy leaves it untouched.
priv rm -rf "$APPS_DIR/PktSwerve.new.app"
priv cp -R PktSwerve.app "$APPS_DIR/PktSwerve.new.app"
priv xattr -dr com.apple.quarantine "$APPS_DIR/PktSwerve.new.app" 2>/dev/null || true
priv codesign --force --deep --sign - "$APPS_DIR/PktSwerve.new.app" 2>/dev/null || true
priv rm -rf "$APP"
priv mv "$APPS_DIR/PktSwerve.new.app" "$APP"
open "$APP"
echo "Installed. Look for the PktSwerve icon in the menu bar."
