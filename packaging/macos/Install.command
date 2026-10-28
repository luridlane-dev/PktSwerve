#!/bin/bash
# Copies PktSwerve to /Applications and clears the download quarantine flag.
cd "$(dirname "$0")" || exit 1

rm -rf /Applications/PktSwerve.app
cp -R PktSwerve.app /Applications/ || exit 1
xattr -dr com.apple.quarantine /Applications/PktSwerve.app
open /Applications/PktSwerve.app
