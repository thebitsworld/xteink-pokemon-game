#!/bin/sh
# Builds and runs the ContentProtection ZIP tests.
# No device or PlatformIO needed.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/contentprotection-tests"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

c++ -std=c++17 -fno-exceptions -Wall -Wextra -Werror -I../../include \
  ../../src/Zip.cpp test_zip.cpp -o "$BUILD_DIR/test_zip"
"$BUILD_DIR/test_zip"
