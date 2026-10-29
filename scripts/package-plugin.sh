#!/bin/sh
# Architecture-independent Noctalia scripts; users install the CLI separately.
set -eu
output=${1:-dist}
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^project(wordspring-runner VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
mkdir -p "$output"
tar -C "$root" -czf "$output/wordspring-noctalia-$version-plugin.tar.gz" wordspring catalog.toml
echo "$output/wordspring-noctalia-$version-plugin.tar.gz"
