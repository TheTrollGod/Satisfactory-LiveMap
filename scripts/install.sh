#!/usr/bin/env bash
# Install or update Live Map. Run it from the game or server install folder:
#
#   wget -qO- https://raw.githubusercontent.com/TheTrollGod/Satisfactory-LiveMap/main/scripts/install.sh | bash
#
# It looks at what's installed there and picks the matching release zip:
#   Linux dedicated server   -> LiveMap-LinuxServer
#   Windows dedicated server -> LiveMap-WindowsServer   (e.g. from Git Bash or WSL)
#   Game client              -> LiveMap-Windows         (Windows, or Linux via Proton)
# On Windows, scripts/install.ps1 does the same from PowerShell.
#
# Stop the game or server before installing or updating. See --help for options.
set -euo pipefail

REPO="TheTrollGod/Satisfactory-LiveMap"
VERSION="latest"
ZIP=""
WEB_DIR=""
TARGET=""
UNINSTALL=0
INSTALL_DIR=""

die()  { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }
warn() { echo "warning: $*" >&2; }

usage() {
  cat << 'EOF'
Install or update Live Map on a Satisfactory dedicated server or game install.

Usage: install.sh [options] [INSTALL_DIR]

INSTALL_DIR is the game or server install folder (the one containing
FactoryGame/). Defaults to the current folder, then a few common locations.

Options:
  --target T      skip detection: linux-server, windows-server or windows
  --version TAG   install a specific release tag (default: latest)
  --zip FILE      install from a local zip instead of downloading
  --web DIR       also download the map page and extract it into DIR
  --uninstall     remove Live Map
  -h, --help      show this help
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --target)    TARGET="${2:?--target needs a value}"; shift 2 ;;
    --version)   VERSION="${2:?--version needs a tag}"; shift 2 ;;
    --zip)       ZIP="${2:?--zip needs a file}"; shift 2 ;;
    --web)       WEB_DIR="${2:?--web needs a directory}"; shift 2 ;;
    --uninstall) UNINSTALL=1; shift ;;
    -h|--help)   usage; exit 0 ;;
    -*)          die "unknown option: $1 (see --help)" ;;
    *)           INSTALL_DIR="$1"; shift ;;
  esac
done

# ---- find the install ----
if [ -z "$INSTALL_DIR" ]; then
  for d in \
    "$PWD" \
    "$HOME/SatisfactoryDedicatedServer" \
    "$HOME/.steam/steam/steamapps/common/SatisfactoryDedicatedServer" \
    "$HOME/.local/share/Steam/steamapps/common/SatisfactoryDedicatedServer" \
    "/home/steam/SatisfactoryDedicatedServer" \
    "/home/steam/.steam/steam/steamapps/common/SatisfactoryDedicatedServer" \
    "/opt/satisfactory" \
    "/config/gamefiles" \
    "$HOME/.steam/steam/steamapps/common/Satisfactory" \
    "$HOME/.local/share/Steam/steamapps/common/Satisfactory"; do
    if [ -d "$d/FactoryGame" ]; then INSTALL_DIR="$d"; break; fi
  done
  [ -n "$INSTALL_DIR" ] || die "couldn't find Satisfactory. Run this from the install folder (the one containing FactoryGame/), or pass it as an argument."
fi
[ -d "$INSTALL_DIR/FactoryGame" ] || die "$INSTALL_DIR has no FactoryGame/ folder; is that the game or server install folder?"
MODS="$INSTALL_DIR/FactoryGame/Mods"
DEST="$MODS/LiveMap"

# ---- work out which build it needs ----
detect_target() {
  local d="$1"
  if [ -f "$d/FactoryServer.sh" ] || [ -d "$d/FactoryGame/Binaries/Linux" ]; then echo linux-server
  elif [ -f "$d/FactoryServer.exe" ]; then echo windows-server
  elif ls "$d"/FactoryGame*.exe >/dev/null 2>&1 || [ -d "$d/FactoryGame/Binaries/Win64" ]; then echo windows
  else
    case "$(uname -s)" in
      Linux) echo linux-server ;;
      *)     echo "" ;;
    esac
  fi
}
if [ -z "$TARGET" ]; then
  TARGET="$(detect_target "$INSTALL_DIR")"
  [ -n "$TARGET" ] || die "couldn't tell what's installed in $INSTALL_DIR; pass --target linux-server, windows-server or windows"
fi
case "$TARGET" in
  linux-server)   ASSET_NAME=LiveMap-LinuxServer;   BIN_HINT=Linux;  KIND="Linux dedicated server" ;;
  windows-server) ASSET_NAME=LiveMap-WindowsServer; BIN_HINT=Win64;  KIND="Windows dedicated server" ;;
  windows)        ASSET_NAME=LiveMap-Windows;       BIN_HINT=Win64;  KIND="game client (Windows build)" ;;
  *) die "unknown --target '$TARGET' (use linux-server, windows-server or windows)" ;;
esac
info "Install: $INSTALL_DIR ($KIND)"

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
asset() { echo "$ASSETS" | grep "/$1-[0-9][^/]*\.zip$" | head -n1; }

# ---- install the mod ----
if [ -z "$ZIP" ]; then
  URL="$(asset "$ASSET_NAME")"
  [ -n "$URL" ] || die "release '$VERSION' has no $ASSET_NAME zip"
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
if [ -d "$SRC/Binaries" ] && [ ! -d "$SRC/Binaries/$BIN_HINT" ]; then
  warn "this zip has no $BIN_HINT binaries, but this looks like a $KIND (expected $ASSET_NAME)"
fi

mkdir -p "$MODS"
rm -rf "$DEST"
cp -r "$SRC" "$DEST"
info "Installed Live Map to $DEST"

if [ ! -d "$MODS/SML" ]; then
  warn "SML isn't installed here ($MODS/SML is missing). Live Map won't load without it."
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
fi

echo
info "Done. Start the world, allow TCP port 8086 through the firewall, then browse to http://<host>:8086/"
