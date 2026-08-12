#!/bin/sh
# Builds a ScriptureGuide .hpkg on Haiku.
#
#   sh package.sh                -> build from THIS checkout
#   sh package.sh v1.2.0         -> clone that tag into a temp dir and
#                                   build from it, then clean up
#   sh package.sh v1.2.0 --install
#   sh package.sh --install
#
# Handed to someone as a single file, the tag form needs nothing else
# present: it installs the build dependencies, fetches the source itself,
# builds, packages, and removes everything it created except the .hpkg.
#
# Deliberately NOT a HaikuPorts recipe. It drives Haiku's own `package`
# tool directly, for handing builds to testers and attaching them to a
# release. See CLAUDE.md for why the recipe route is not used here.

set -e

REPO="https://github.com/Paradoxianer/ScriptureGuide.git"

TAG=""
INSTALL=""
for arg in "$@"; do
	case "$arg" in
		--install) INSTALL=yes ;;
		-*) echo "Unbekannte Option: $arg" >&2; exit 1 ;;
		*) TAG="$arg" ;;
	esac
done

TOTAL=5
step() { echo; echo "== $1/$TOTAL: $2 =="; }

# Where the .hpkg ends up: next to this script for a checkout build, in
# the current directory when building a tag (the checkout is temporary
# and about to be deleted).
if [ -n "$TAG" ]; then
	OUT="$(pwd)"
else
	OUT="$(cd "$(dirname "$0")" && pwd)/build-package"
fi

# --- 1. dependencies ----------------------------------------------------
step 1 "Abhaengigkeiten installieren"
# -y so an unattended run doesn't stall on a prompt. Already-installed
# packages are a no-op, so this is safe to run repeatedly.
pkgman install -y git haiku_devel devel:libsword_1.8.1 lib:libsword_1.8.1 \
	devel:libz makefile_engine cmd:wget cmd:unzip cmd:awk

# --- 2. source ----------------------------------------------------------
WORK=""
if [ -n "$TAG" ]; then
	step 2 "Quellcode holen ($TAG)"
	WORK=$(mktemp -d "$HOME/scriptureguide-build.XXXXXX")
	# Removed however this exits -- including Ctrl-C -- so a failed run
	# doesn't leave a checkout behind in $HOME.
	trap 'rm -rf "$WORK"' EXIT INT TERM
	git clone --branch "$TAG" --depth 1 "$REPO" "$WORK/src"
	SRC="$WORK/src"
else
	step 2 "Quellcode: dieser Checkout"
	SRC="$(cd "$(dirname "$0")" && pwd)"
	echo "   $SRC"
fi

cd "$SRC"

# --- version, read from the app's own resource definition ---------------
# Single source of truth: bumping ScriptureGuide.rdef is enough, this
# script never carries its own copy of the number to forget to update.
rdef="ScriptureGuide/ScriptureGuide.rdef"
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' "$rdef" | head -1)
VERSION="$major.$middle.$minor"
REVISION=1
ARCH=$(getarch 2>/dev/null || echo x86_64)
echo "   ScriptureGuide $VERSION-$REVISION ($ARCH)"

STAGE="$SRC/build-package/stage"

# --- 3. build -----------------------------------------------------------
step 3 "Bauen"
echo "   ScriptureGuide…"
( cd ScriptureGuide && make )
# `make` alone only builds the binary -- the translations in
# locales/*.catkeys are embedded by bindcatalogs.
( cd ScriptureGuide && make bindcatalogs )
echo "   ScriptureGuideManager…"
( cd ScriptureGuideManager && make )
( cd ScriptureGuideManager && make bindcatalogs )

# --- 4. staging tree ----------------------------------------------------
step 4 "Dateibaum zusammenstellen"
rm -rf "$STAGE"
mkdir -p "$STAGE/apps/ScriptureGuide"
mkdir -p "$STAGE/data/deskbar/menu/Applications"
mkdir -p "$STAGE/documentation/packages/scriptureguide"

cp App/ScriptureGuide App/ScriptureGuideManager "$STAGE/apps/ScriptureGuide/"
[ -d App/docs ] && cp -R App/docs "$STAGE/documentation/packages/scriptureguide/"
for f in README.md CHANGELOG.md App/README.htm App/INSTALL.htm App/LICENSE; do
	[ -f "$f" ] && cp "$f" "$STAGE/documentation/packages/scriptureguide/"
done

# Deskbar entries. Relative symlinks, so they stay correct wherever the
# package is activated (/boot/system or /boot/home/config).
( cd "$STAGE/data/deskbar/menu/Applications" &&
	ln -sf ../../../../apps/ScriptureGuide/ScriptureGuide ScriptureGuide &&
	ln -sf ../../../../apps/ScriptureGuide/ScriptureGuideManager ScriptureGuideManager )

# Requirements are read from the binaries rather than hard-coded: a
# hand-written list rots the first time a library is added or dropped,
# and the symptom is a package that installs cleanly and won't launch.
requires=""
for bin in "$STAGE/apps/ScriptureGuide/"*; do
	for lib in $(readelf -d "$bin" 2>/dev/null |
			sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
		case "$lib" in
			# Part of Haiku itself; covered by the plain "haiku" requirement.
			libbe.so|libroot.so|libnetwork.so|libtranslation.so|\
			libtracker.so|libgame.so|libmedia.so|libdevice.so|\
			libtextencoding.so|libstdc++.so*|libsupc++.so*|libgcc_s.so*)
				;;
			*)
				# libfoo-1.2.3.so and libfoo.so.1 both become lib:libfoo_1
				name=$(echo "$lib" | sed 's/\.so.*//; s/-/_/g')
				requires="$requires
	lib:$name"
				;;
		esac
	done
done
# Each entry already carries its own leading tab. Interpolated on a line
# of its own below, because $( ) strips the leading newline and "haiku"
# would otherwise share a line with the first library.
requires=$(echo "$requires" | sort -u | sed '/^$/d')
echo "   Abhaengigkeiten:$(echo "$requires" | tr -d '\t' | tr '\n' ' ')"

cat > "$STAGE/.PackageInfo" <<EOF
name			scriptureguide
version			$VERSION-$REVISION
architecture	$ARCH
summary			"Bible study program for Haiku"
description		"ScriptureGuide (formerly Be-Logos) is a Bible study program \\
built on the SWORD library. It shows any number of Bible, commentary and \\
personal-notes columns side by side, with verse-aligned rows, and ships \\
with a companion manager for downloading SWORD modules."
packager		"ScriptureGuide contributors"
vendor			"ScriptureGuide contributors"

copyrights {
	"2003-2026 ScriptureGuide contributors"
}

licenses {
	"GNU GPL v2"
}

provides {
	scriptureguide = $VERSION
	app:ScriptureGuide = $VERSION
	app:ScriptureGuideManager = $VERSION
}

requires {
	haiku
$requires
	cmd:wget
	cmd:unzip
}
EOF

# --- 5. package ---------------------------------------------------------
step 5 "Paket bauen"
mkdir -p "$OUT"
HPKG="$OUT/scriptureguide-$VERSION-$REVISION-$ARCH.hpkg"
rm -f "$HPKG"
( cd "$STAGE" && package create -b "$HPKG" >/dev/null &&
	package add -f "$HPKG" .PackageInfo apps data documentation >/dev/null )

# The staging tree has served its purpose; the .hpkg is the artifact. For
# a tag build the whole checkout goes too, via the EXIT trap above.
rm -rf "$STAGE"
[ -n "$TAG" ] || rmdir "$SRC/build-package" 2>/dev/null || true

echo
echo "Fertig: $HPKG"

if [ -n "$INSTALL" ]; then
	echo
	echo "== Installieren =="
	pkgman install -y "$HPKG"
fi
