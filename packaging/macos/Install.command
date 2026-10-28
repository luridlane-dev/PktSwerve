#!/bin/bash
# Copies PktSwerve to /Applications and clears the download quarantine flag.
set -e
cd "$(dirname "$0")"

# Copy beside the old app first so a failed copy leaves it untouched.
rm -rf /Applications/PktSwerve.new.app
cp -R PktSwerve.app /Applications/PktSwerve.new.app
xattr -dr com.apple.quarantine /Applications/PktSwerve.new.app
rm -rf /Applications/PktSwerve.app
mv /Applications/PktSwerve.new.app /Applications/PktSwerve.app
open /Applications/PktSwerve.app
