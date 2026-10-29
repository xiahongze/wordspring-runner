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

# The Noctalia distribution has no KDE runtime dependency. The same shared core
# is statically linked into its Qt Core + SQLite CLI.
noctalia_name="wordspring-noctalia-$version-$platform"
noctalia_package="$stage/$noctalia_name"
mkdir -p "$noctalia_package/bin" "$noctalia_package/share/wordspring" "$noctalia_package/share/noctalia/plugins"
cp "$stage/root/usr/bin/wordspring-lookup" "$noctalia_package/bin/"
cp "$stage/root/usr/share/wordspring/import_dictionary.py" "$noctalia_package/share/wordspring/"
cp -R "$stage/root/usr/share/noctalia/plugins/wordspring" "$noctalia_package/share/noctalia/plugins/"
cp "$root/scripts/install-noctalia.sh" "$noctalia_package/install.sh"
cp "$root/wordspring/README.md" "$root/LICENSE" "$root/importer/requirements-apple.txt" "$noctalia_package/"
chmod 755 "$noctalia_package/install.sh"
tar -C "$stage" -czf "$output/$noctalia_name.tar.gz" "$noctalia_name"
echo "$output/$noctalia_name.tar.gz"
