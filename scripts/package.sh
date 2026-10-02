#!/bin/sh
# Package a /usr build into a relocatable tarball: scripts/package.sh BUILD_DIR OUTPUT_DIR PLATFORM
set -eu

build=$1
output=$2
platform=$3
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^project(wordspring-runner VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
name="wordspring-runner-$version-$platform"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

DESTDIR="$stage/root" cmake --install "$build" >/dev/null
package="$stage/$name"
mkdir -p "$package"
cp -R "$stage/root/usr/bin" "$stage/root/usr/lib" "$stage/root/usr/share" "$package/"
cp "$root/scripts/install.sh" "$package/install.sh"
cp "$root/README.md" "$root/LICENSE" "$root/importer/requirements-apple.txt" "$package/"
chmod 755 "$package/install.sh"

mkdir -p "$output"
tar -C "$stage" -czf "$output/$name.tar.gz" "$name"
echo "$output/$name.tar.gz"
