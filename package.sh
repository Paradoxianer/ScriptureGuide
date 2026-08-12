#!/bin/sh
# Builds a ScriptureGuide .hpkg from this checkout, on Haiku.
#
#   sh package.sh            -> scriptureguide-<version>-1-<arch>.hpkg
#   sh package.sh --install  -> ... and installs it with pkgman
#
# Deliberately NOT a HaikuPorts recipe. This produces a package directly
# with Haiku's own `package` tool, for handing to testers and for
# attaching to a release. See CLAUDE.md for why the recipe route is not
# used here.
#
# Run it on Haiku, from the root of the checkout. Everything lands in
# ./build-package/ and nothing outside it is touched.

set -e

cd "$(dirname "$0")"
ROOT="$(pwd)"
STAGE="$ROOT/build-package/stage"
OUT="$ROOT/build-package"

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

echo "== ScriptureGuide $VERSION-$REVISION ($ARCH) =="

# --- build --------------------------------------------------------------
echo "== 1/4: Bauen =="
( cd ScriptureGuide && make )
# `make` alone only builds the binary -- the translations in
# locales/*.catkeys are embedded by bindcatalogs.
( cd ScriptureGuide && make bindcatalogs )
( cd ScriptureGuideManager && make )
( cd ScriptureGuideManager && make bindcatalogs )

# --- staging tree -------------------------------------------------------
echo "== 2/4: Dateibaum zusammenstellen =="
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

# --- requirements, read from the binaries -------------------------------
# Derived rather than hard-coded: a hand-written list silently rots the
# first time a library is added or dropped, and the failure shows up as a
# package that installs but won't launch.
echo "== 3/4: Abhaengigkeiten ermitteln =="
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
echo "   $(echo "$requires" | tr -d '\t' | tr '\n' ' ')"

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

# --- build the package --------------------------------------------------
echo "== 4/4: Paket bauen =="
HPKG="$OUT/scriptureguide-$VERSION-$REVISION-$ARCH.hpkg"
rm -f "$HPKG"
( cd "$STAGE" && package create -b "$HPKG" >/dev/null && package add -f "$HPKG" \
	.PackageInfo apps data documentation >/dev/null )

echo
echo "Fertig: $HPKG"
package list "$HPKG" | head -20

if [ "$1" = "--install" ]; then
	echo
	echo "== Installieren =="
	pkgman install -y "$HPKG"
fi
