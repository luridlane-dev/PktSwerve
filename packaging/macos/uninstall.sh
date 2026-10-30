#!/bin/bash
# PktSwerve macOS uninstaller. Stops the app and its root helper, removes the
# pf rules, the app, the login item and (unless told otherwise) the settings
# and logs, then reports anything that is still left.
#
#   curl -fsSL https://raw.githubusercontent.com/luridlane-dev/PktSwerve/main/packaging/macos/uninstall.sh | bash
#
# Options (pass after `bash -s --`):
#   --keep-settings   keep config and logs, e.g. before reinstalling an update
#   --clean-only      only clear stale leftovers (stray processes, pf rules,
#                     sockets); keep the app, login item and settings.
#                     install.sh runs this before every install.
#   --dry-run         only print what would be removed
set -uo pipefail

APPS_DIR="${PKTSWERVE_APPS_DIR:-/Applications}"
TMP_DIR="${PKTSWERVE_TMP_DIR:-/tmp}"
AGENT="$HOME/Library/LaunchAgents/com.pktswerve.app.plist"
KEEP_SETTINGS=0
CLEAN_ONLY=0
DRY=0

for arg in "$@"; do
    case "$arg" in
        --keep-settings) KEEP_SETTINGS=1 ;;
        --clean-only)    CLEAN_ONLY=1 ;;
        --dry-run)       DRY=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1m==>\033[0m %s\n' "$*"; }
fail() { printf '\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Darwin" ] || fail "This uninstaller is for macOS only."
[ -n "${HOME:-}" ] && [ "$HOME" != "/" ] || fail "HOME is not set."

# Runs a command, or only prints it with --dry-run.
act() {
    if [ "$DRY" = 1 ]; then printf '    would run: %s\n' "$*"; else "$@" >/dev/null 2>&1; fi
}

# A first install has nothing to clean, so do not ask for a password for nothing.
if [ "$CLEAN_ONLY" = 1 ] && [ "$DRY" = 0 ]; then
    if ! pgrep -x PktSwerve >/dev/null 2>&1 \
       && ! ls "$TMP_DIR"/pktswerve-*.sock >/dev/null 2>&1 \
       && [ ! -e "$APPS_DIR/PktSwerve.app" ] && [ ! -e "$APPS_DIR/PktSwerve.new.app" ]; then
        say "No earlier PktSwerve found, nothing to clean"
        exit 0
    fi
fi

if [ "$DRY" = 0 ]; then
    if [ "$CLEAN_ONLY" = 1 ]; then
        say "Checking for leftovers of an earlier PktSwerve (administrator password needed)"
    else
        say "Administrator password needed to remove the network rules and the app"
    fi
    sudo -v || fail "Could not get administrator rights."
fi

if [ "$CLEAN_ONLY" = 0 ]; then
    say "Removing the login item"
    act launchctl bootout "gui/$(id -u)/com.pktswerve.app"
    act rm -f "$AGENT"
fi

say "Stopping PktSwerve"
act pkill -x PktSwerve
act sudo pkill -x PktSwerve
if [ "$DRY" = 0 ]; then
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -x PktSwerve >/dev/null 2>&1 || break
        sleep 0.5
    done
    act sudo pkill -9 -x PktSwerve
fi

say "Removing the pf redirect rules"
# Each run of the app loads its rules into its own anchor, com.apple/250.PktSwerve.<pid>.
anchors=""
if [ "$DRY" = 1 ]; then
    echo "    (loaded pf rules can only be listed with administrator rights, so a dry run skips them)"
else
    anchors=$(sudo pfctl -a com.apple -s Anchors 2>/dev/null | sed -n 's/.*\(250\.PktSwerve[0-9.]*\)$/\1/p')
fi
for a in $anchors; do
    act sudo pfctl -a "com.apple/$a" -F all
done
# Leftover cleanup watchdogs would only repeat what was just done.
act sudo pkill -f '250\.PktSwerve'
act sudo rm -f "$TMP_DIR"/pktswerve-*.sock

if [ "$CLEAN_ONLY" = 1 ]; then
    # A copy interrupted halfway through an earlier install.
    act sudo rm -rf "$APPS_DIR/PktSwerve.new.app"
else
    say "Removing the app"
    act sudo rm -rf "$APPS_DIR/PktSwerve.app" "$APPS_DIR/PktSwerve.new.app" "$HOME/Applications/PktSwerve.app"

    if [ "$KEEP_SETTINGS" = 1 ]; then
        say "Keeping settings and logs (--keep-settings)"
    else
        say "Removing settings, logs and caches"
        lib="$HOME/Library"
        for dir in "$lib/Application Support" "$lib/Preferences" "$lib/Caches" "$lib/Logs" "$lib/Saved Application State"; do
            [ -d "$dir" ] || continue
            # Only direct children whose name mentions PktSwerve, e.g. PktSwerve, com.pktswerve.app.plist.
            while IFS= read -r item; do
                act sudo rm -rf "$item"
            done < <(find "$dir" -maxdepth 1 -iname '*pktswerve*' 2>/dev/null)
        done
    fi
fi

say "Clearing the DNS cache"
act sudo dscacheutil -flushcache
act sudo killall -HUP mDNSResponder

[ "$DRY" = 1 ] && { say "Dry run finished, nothing was changed."; exit 0; }

# Check that nothing is left behind.
left=()
pgrep -x PktSwerve >/dev/null 2>&1 && left+=("a PktSwerve process is still running")
sudo pfctl -a com.apple -s Anchors 2>/dev/null | grep -q 'PktSwerve' && left+=("a pf anchor named PktSwerve is still loaded")
if [ "$CLEAN_ONLY" = 0 ]; then
    [ -e "$APPS_DIR/PktSwerve.app" ] && left+=("$APPS_DIR/PktSwerve.app")
    [ -e "$AGENT" ] && left+=("$AGENT")
    if [ "$KEEP_SETTINGS" = 0 ]; then
        for dir in "$HOME/Library/Application Support" "$HOME/Library/Preferences" "$HOME/Library/Caches" "$HOME/Library/Logs"; do
            [ -d "$dir" ] && while IFS= read -r item; do left+=("$item"); done < <(find "$dir" -maxdepth 1 -iname '*pktswerve*' 2>/dev/null)
        done
    fi
fi

if [ ${#left[@]} -eq 0 ]; then
    if [ "$CLEAN_ONLY" = 1 ]; then say "No leftovers"; else say "PktSwerve is completely removed."; fi
    exit 0
fi
printf '\033[33mStill left:\033[0m\n'
printf '  - %s\n' "${left[@]}"
exit 1
