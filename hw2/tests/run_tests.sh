#!/bin/sh
# Run from any directory; Python applies a timeout to every child process.
set -eu
cd "$(dirname "$0")/.."
exec make test
