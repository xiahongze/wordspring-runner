#!/bin/sh
# Install a Wordspring Noctalia release for the current user. No system packages.
set -eu

action=install
case ${1:-} in
  '') ;;
  --uninstall) action=uninstall ;;
  -h|--help)
    echo 'Usage: ./install.sh [--uninstall]'
    echo 'Installs wordspring-lookup under PREFIX (default ~/.local) and the plugin under XDG_DATA_HOME.'
    exit 0 ;;
  *) echo 'Usage: ./install.sh [--uninstall]' >&2; exit 2 ;;
esac

here=$(cd "$(dirname "$0")" && pwd)
prefix=${PREFIX:-$HOME/.local}
data=${XDG_DATA_HOME:-$HOME/.local/share}
plugin=$data/noctalia/plugins/wordspring
cli=$prefix/bin/wordspring-lookup

if [ "$action" = uninstall ]; then
  if command -v noctalia >/dev/null 2>&1; then noctalia msg plugins disable xiahongze/wordspring >/dev/null 2>&1 || true; fi
  rm -f "$cli"
  rm -rf "$plugin"
  echo 'Removed the Noctalia frontend and CLI. Dictionaries, bookmarks, history, and shared settings are kept.'
  echo 'The dictionary importer is kept because KRunner may also use it.'
  exit 0
fi

install -Dm755 "$here/bin/wordspring-lookup" "$cli"
install -Dm644 "$here/share/wordspring/import_dictionary.py" "$data/wordspring/import_dictionary.py"
mkdir -p "$plugin"
cp -R "$here/share/noctalia/plugins/wordspring/." "$plugin/"
echo "Installed Wordspring CLI: $cli"
echo "Installed Noctalia plugin: $plugin"
echo 'Set the plugin CLI path if your shell does not include PREFIX/bin in PATH.'
echo 'Enable with: noctalia msg plugins enable xiahongze/wordspring'
echo 'Then type /def word in the launcher, or add the Wordspring bar widget in Settings.'
