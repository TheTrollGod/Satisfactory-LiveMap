#!/usr/bin/env bash
# Install or update Live Map on a Linux Satisfactory dedicated server.
#
#   wget -qO- https://raw.githubusercontent.com/TheTrollGod/Satisfactory-LiveMap/main/scripts/install.sh | bash -s -- [options] [SERVER_DIR]
#
# SERVER_DIR is the dedicated server's install folder (the one containing
# FactoryGame/). If left out, a few common locations are tried.
#
# Options:
#   --version TAG   install a specific release tag (default: latest)
#   --zip FILE      install from a local LiveMap-LinuxServer zip instead of downloading
#   --web DIR       also download the map page and extract it into DIR
#   --uninstall     remove Live Map from the server
#   -h, --help      show this help
#
# Stop the server before installing or updating, and start it again afterwards.
set -euo pipefail

REPO="TheTrollGod/Satisfactory-LiveMap"
VERSION="latest"
ZIP=""
WEB_DIR=""
UNINSTALL=0
SERVER_DIR=""

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
warn() { echo "warning: $*" >&2; }

usage() {
  cat << 'EOF'
Install or update Live Map on a Linux Satisfactory dedicated server.

Usage: install.sh [options] [SERVER_DIR]

SERVER_DIR is the server's install folder (the one containing FactoryGame/).
If left out, a few common locations are tried.

Options:
  --version TAG   install a specific release tag (default: latest)
  --zip FILE      install from a local LiveMap-LinuxServer zip instead of downloading
  --web DIR       also download the map page and extract it into DIR
  --uninstall     remove Live Map from the server
  -h, --help      show this help
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --version)   VERSION="${2:?--version needs a tag}"; shift 2 ;;
    --zip)       ZIP="${2:?--zip needs a file}"; shift 2 ;;
    --web)       WEB_DIR="${2:?--web needs a directory}"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help)   usage; exit 0 ;;
    -*)          die "unknown option: $1 (see --help)" ;;
    *)           SERVER_DIR="$1"; shift ;;
  esac
done

# ---- find the server ----
if [ -z "$SERVER_DIR" ]; then
  for d in \
    "$PWD" \
    "$HOME/SatisfactoryDedicatedServer" \
    "$HOME/.steam/steam/steamapps/common/SatisfactoryDedicatedServer" \
    "$HOME/.local/share/Steam/steamapps/common/SatisfactoryDedicatedServer" \
    "/home/steam/SatisfactoryDedicatedServer" \
    "/home/steam/.steam/steam/steamapps/common/SatisfactoryDedicatedServer" \
    "/opt/satisfactory" \
    "/config/gamefiles"; do
    if [ -d "$d/FactoryGame" ]; then SERVER_DIR="$d"; break; fi
  done
  [ -n "$SERVER_DIR" ] || die "couldn't find the server. Pass its folder (the one containing FactoryGame/) as an argument."
fi
[ -d "$SERVER_DIR/FactoryGame" ] || die "$SERVER_DIR has no FactoryGame/ folder; is that the dedicated server's install folder?"
MODS="$SERVER_DIR/FactoryGame/Mods"
DEST="$MODS/LiveMap"
info "Server: $SERVER_DIR"

if [ "$UNINSTALL" = 1 ]; then
  if [ -d "$DEST" ]; then rm -rf "$DEST"; info "Removed $DEST"; else info "Live Map isn't installed."; fi
  exit 0
fi

# ---- tools ----
if command -v wget >/dev/null; then
  fetch()    { wget -q -O "$2" "$1"; }
  fetch_to() { wget -q -O - "$1"; }
elif command -v curl >/dev/null; then
  fetch()    { curl -fsSL -o "$2" "$1"; }
  fetch_to() { curl -fsSL "$1"; }
else
  die "needs wget or curl"
fi

unzip_to() { # zip dir
  if command -v unzip >/dev/null; then unzip -q -o "$1" -d "$2"
  elif command -v python3 >/dev/null; then python3 -m zipfile -e "$1" "$2"
  elif command -v bsdtar >/dev/null; then bsdtar -xf "$1" -C "$2"
  else die "needs unzip (e.g. sudo apt install unzip)"; fi
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- pick the release assets ----
ASSETS=""
if [ -z "$ZIP" ] || [ -n "$WEB_DIR" ]; then
  if [ "$VERSION" = "latest" ]; then API="https://api.github.com/repos/$REPO/releases/latest"
  else API="https://api.github.com/repos/$REPO/releases/tags/$VERSION"; fi
  ASSETS="$(fetch_to "$API" | grep -o '"browser_download_url": *"[^"]*"' | sed 's/.*"\(https[^"]*\)"/\1/')" \
    || die "couldn't read release '$VERSION' from GitHub"
  [ -n "$ASSETS" ] || die "release '$VERSION' has no files"
fi
asset() { echo "$ASSETS" | grep "/$1-[^/]*\.zip$" | head -n1; }

# ---- install the mod ----
if [ -z "$ZIP" ]; then
  URL="$(asset LiveMap-LinuxServer)"
  [ -n "$URL" ] || die "release '$VERSION' has no LiveMap-LinuxServer zip"
  info "Downloading $(basename "$URL")"
  ZIP="$TMP/livemap.zip"
  fetch "$URL" "$ZIP"
fi
[ -f "$ZIP" ] || die "no such file: $ZIP"

mkdir -p "$TMP/mod"
unzip_to "$ZIP" "$TMP/mod"
# Accept zips with LiveMap.uplugin at the top or one folder down.
UPLUGIN="$(find "$TMP/mod" -maxdepth 2 -name LiveMap.uplugin | head -n1)"
[ -n "$UPLUGIN" ] || die "that zip doesn't contain LiveMap.uplugin"
SRC="$(dirname "$UPLUGIN")"
if [ -d "$SRC/Binaries" ] && ! find "$SRC/Binaries" -path '*Linux*' -print -quit | grep -q .; then
  warn "this zip has no Linux binaries; did you mean the LiveMap-LinuxServer zip?"
fi

mkdir -p "$MODS"
rm -rf "$DEST"
cp -r "$SRC" "$DEST"
info "Installed Live Map to $DEST"

if [ ! -d "$MODS/SML" ]; then
  warn "SML isn't installed on this server ($MODS/SML is missing). Live Map won't load without it."
  warn "Install it with Satisfactory Mod Manager or ficsit-cli: https://docs.ficsit.app"
fi

# ---- map page ----
if [ -n "$WEB_DIR" ]; then
  URL="$(asset LiveMap-Web)"
  [ -n "$URL" ] || die "release '$VERSION' has no LiveMap-Web zip"
  info "Downloading $(basename "$URL")"
  fetch "$URL" "$TMP/web.zip"
  mkdir -p "$WEB_DIR"
  unzip_to "$TMP/web.zip" "$WEB_DIR"
  info "Map page extracted to $WEB_DIR"
  echo "    Serve it with:  cd '$WEB_DIR' && python3 -m http.server 5173"
fi

echo
info "Done. Start the server, open TCP port 8086, then browse to http://<server-ip>:8086/"
