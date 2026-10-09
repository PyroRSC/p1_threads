#!/usr/bin/env sh
# Builds the submission zip: four variant folders plus the README and top-level CMakeLists.
# Usage: ./package.sh [output.zip]
set -eu

cd "$(dirname "$0")"
OUTPUT="${1:-p1_threads.zip}"

rm -f "$OUTPUT"
zip -r "$OUTPUT" README.md CMakeLists.txt \
    variant1_immediate_range variant2_deferred_range \
    variant3_immediate_divisibility variant4_deferred_divisibility \
    -x '*/build/*' '*.o' '*.exe' '*/variant[1-4]' '*/out.txt' '.DS_Store' '*/.DS_Store'

echo "Created $OUTPUT"
