#!/bin/sh

# This script remains for backwards compatibility, but consists of a single
# git command.

cd "$(dirname "$0")" || exit 1
git clean -fx .

echo "WARNING: This script has been split with libraries/clean-source-libs.sh"
echo "If you want to fix a problem with bundled libs, it's the other script"
echo "you want to invoke."

echo "WARNING: This script is deprecated and might be removed in a future"
echo "release. You can run 'git clean -f build source' from the repository"
echo "root to clean up build workspaces."
