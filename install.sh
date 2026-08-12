#!/bin/sh
# Downloads, builds and installs ScriptureGuide on Haiku -- standalone,
# does not need a pre-existing checkout (clones its own into a temp
# location under $HOME). Meant to be handed to someone as a single
# file (e.g. a tester on a Haiku release not covered by an official
# HaikuDepot package build yet, where a nightly-built .hpkg's own
# declared "haiku >=" requirement refuses to install) -- download this
# one file, run it, done.
#
# Runs the same steps a HaikuPorts recipe would (see this project's
# haikuports fork under haiku-apps/scriptureguide), just without going
# through haikuporter/a chroot.
#
# Usage: sh install.sh
#
# TAG below tracks the release this script was last updated for --
# bump it (and re-test) when cutting a new release meant for this kind
# of manual distribution.

set -e

TAG="v1.2.2"
REPO="https://github.com/Paradoxianer/ScriptureGuide.git"
SRC_DIR="$HOME/ScriptureGuide-source"
INSTALL_DIR="$HOME/apps/ScriptureGuide"

echo "== 1/4: Abhaengigkeiten installieren =="
pkgman install -y git haiku_devel devel:libsword_1.8.1 lib:libsword_1.8.1 \
	devel:libz makefile_engine cmd:wget cmd:unzip cmd:awk

echo "== 2/4: Quellcode holen ($TAG) =="
rm -rf "$SRC_DIR"
git clone --branch "$TAG" --depth 1 "$REPO" "$SRC_DIR"
cd "$SRC_DIR"

echo "== 3/4: Bauen =="
cd ScriptureGuide
make
# `make` allein baut nur die Binary -- die Uebersetzungen aus
# locales/*.catkeys werden erst durch bindcatalogs eingebettet.
make bindcatalogs
cd ../ScriptureGuideManager
make
cd ..

echo "== 4/4: Installieren nach $INSTALL_DIR =="
mkdir -p "$INSTALL_DIR"
cp -R App/ScriptureGuide App/ScriptureGuideManager "$INSTALL_DIR/"
[ -d App/docs ] && cp -R App/docs "$INSTALL_DIR/"
[ -f App/README.htm ] && cp App/README.htm "$INSTALL_DIR/"
[ -f App/INSTALL.htm ] && cp App/INSTALL.htm "$INSTALL_DIR/"

mkdir -p "$HOME/Desktop"
ln -sf "$INSTALL_DIR/ScriptureGuide" "$HOME/Desktop/ScriptureGuide"
ln -sf "$INSTALL_DIR/ScriptureGuideManager" "$HOME/Desktop/ScriptureGuideManager"

echo
echo "Fertig. Start ueber die Desktop-Symbole 'ScriptureGuide' /"
echo "'ScriptureGuideManager', oder direkt:"
echo "  $INSTALL_DIR/ScriptureGuide"
