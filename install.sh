#!/bin/sh
# Builds and installs ScriptureGuide from this checkout, on Haiku.
#
# Runs the same steps a HaikuPorts recipe would (see this project's
# haikuports fork under haiku-apps/scriptureguide), just without going
# through haikuporter/a chroot -- meant for a machine that isn't (yet)
# covered by an official HaikuDepot package build, e.g. an older
# release where a nightly-built .hpkg's own declared "haiku >="
# requirement refuses to install, or simply for building straight from
# a git checkout without packaging.
#
# Usage: sh install.sh   (from anywhere -- resolves paths relative to
#                          this script's own location)

set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALL_DIR="$HOME/apps/ScriptureGuide"

echo "== 1/3: Abhaengigkeiten installieren =="
pkgman install -y git haiku_devel devel:libsword_1.8.1 lib:libsword_1.8.1 \
	devel:libz makefile_engine cmd:wget cmd:unzip cmd:awk

echo "== 2/3: Bauen =="
cd "$ROOT/ScriptureGuide"
make
# `make` allein baut nur die Binary -- die Uebersetzungen aus
# locales/*.catkeys werden erst durch bindcatalogs eingebettet.
make bindcatalogs
cd "$ROOT/ScriptureGuideManager"
make

echo "== 3/3: Installieren nach $INSTALL_DIR =="
mkdir -p "$INSTALL_DIR"
cp -R "$ROOT/App/ScriptureGuide" "$ROOT/App/ScriptureGuideManager" "$INSTALL_DIR/"
[ -d "$ROOT/App/docs" ] && cp -R "$ROOT/App/docs" "$INSTALL_DIR/"
[ -f "$ROOT/App/README.htm" ] && cp "$ROOT/App/README.htm" "$INSTALL_DIR/"
[ -f "$ROOT/App/INSTALL.htm" ] && cp "$ROOT/App/INSTALL.htm" "$INSTALL_DIR/"

mkdir -p "$HOME/Desktop"
ln -sf "$INSTALL_DIR/ScriptureGuide" "$HOME/Desktop/ScriptureGuide"
ln -sf "$INSTALL_DIR/ScriptureGuideManager" "$HOME/Desktop/ScriptureGuideManager"

echo
echo "Fertig. Start ueber die Desktop-Symbole 'ScriptureGuide' /"
echo "'ScriptureGuideManager', oder direkt:"
echo "  $INSTALL_DIR/ScriptureGuide"
