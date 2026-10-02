#!/bin/sh
# Install or remove a prebuilt Wordspring Runner tarball.
set -eu

usage() {
    cat <<'EOF'
Usage: ./install.sh [--system] [--uninstall]

Without options, installs for the current user under ~/.local (override with
PREFIX=/some/prefix) and adds a Plasma login script so KRunner finds the plugin.

  --system     install into /usr and the system Qt plugin directory (run with sudo)
  --uninstall  remove the files this script installs; imported dictionaries,
               bookmarks, history and settings are kept
EOF
}

mode=user
action=install
for arg in "$@"; do
    case $arg in
        --system) mode=system ;;
        --uninstall) action=uninstall ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

here=$(cd "$(dirname "$0")" && pwd)

system_plugin_dir() {
    for tool in qtpaths6 qtpaths /usr/lib/qt6/bin/qtpaths6 /usr/lib/qt6/bin/qtpaths /usr/lib64/qt6/bin/qtpaths6; do
        if command -v "$tool" >/dev/null 2>&1; then
            dir=$("$tool" --query QT_INSTALL_PLUGINS 2>/dev/null || true)
            if [ -n "$dir" ] && [ -d "$dir" ]; then echo "$dir"; return; fi
        fi
    done
    for dir in /usr/lib/qt6/plugins /usr/lib64/qt6/plugins /usr/lib/*-linux-gnu/qt6/plugins; do
        if [ -d "$dir/kf6/krunner" ]; then echo "$dir"; return; fi
    done
    echo "Could not find the Qt 6 plugin directory; install qt6 tools or use a user install." >&2
    exit 1
}

if [ "$mode" = system ]; then
    if [ "$(id -u)" -ne 0 ]; then echo "System install needs root: sudo ./install.sh --system" >&2; exit 1; fi
    prefix=/usr
    plugins=$(system_plugin_dir)
    env_script=
else
    prefix=${PREFIX:-$HOME/.local}
    plugins=$prefix/lib/qt6/plugins
    env_script=${XDG_CONFIG_HOME:-$HOME/.config}/plasma-workspace/env/wordspring.sh
fi

runner=$plugins/kf6/krunner/krunner_wordspring.so
kcm=$plugins/kf6/krunner/kcms/kcm_krunner_wordspring.so
viewer=$prefix/bin/wordspring
importer=$prefix/share/wordspring/import_dictionary.py

if [ "$action" = uninstall ]; then
    rm -f "$runner" "$kcm" "$viewer" "$importer"
    [ -n "$env_script" ] && rm -f "$env_script"
    echo "Removed Wordspring Runner from $prefix."
    echo "Imported dictionaries and bookmarks remain in ${XDG_DATA_HOME:-$HOME/.local/share}/wordspring."
    exit 0
fi

install -Dm755 "$here/bin/wordspring" "$viewer"
install -Dm755 "$here/lib/qt6/plugins/kf6/krunner/krunner_wordspring.so" "$runner"
install -Dm755 "$here/lib/qt6/plugins/kf6/krunner/kcms/kcm_krunner_wordspring.so" "$kcm"
install -Dm644 "$here/share/wordspring/import_dictionary.py" "$importer"

if [ -n "$env_script" ]; then
    mkdir -p "$(dirname "$env_script")"
    cat > "$env_script" <<EOF
# Plasma sources this on login so KRunner can find the per-user Wordspring plugin.
export QT_PLUGIN_PATH="$plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
export PATH="$prefix/bin:\$PATH"
EOF
fi

echo "Installed Wordspring Runner into $prefix."
if [ -n "$env_script" ]; then
    echo "Log out and back in so Plasma picks up $env_script, then type 'define word' in KRunner."
else
    echo "Restart KRunner (kquitapp6 krunner) or log out and back in, then type 'define word' in KRunner."
fi
echo "Add dictionaries with: $viewer --settings"
