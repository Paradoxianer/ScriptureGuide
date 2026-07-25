#!/bin/bash
# Dev helper for ScriptureGuide on Haiku. Keeps every remote action behind a
# small, fixed set of subcommands so the *local* ssh/scp invocations stay
# textually identical and are easy to permanently allow.
#
# Usage (from the local machine):
#   ssh haiku ~/repos/ScriptureGuide/dev.sh build
#   ssh haiku ~/repos/ScriptureGuide/dev.sh run
#   ssh haiku ~/repos/ScriptureGuide/dev.sh shot
#   ssh haiku ~/repos/ScriptureGuide/dev.sh log
#   ssh haiku ~/repos/ScriptureGuide/dev.sh kill
#   scp haiku:~/repos/ScriptureGuide/screenshot.png <local-scratch>/screenshot.png

set -e

ROOT=~/repos/ScriptureGuide
LOG=~/sg-run.log
SHOT="$ROOT/screenshot.png"

kill_matching() {
	# $1: pattern to match in `ps` output
	ps | grep -i "$1" | grep -v grep | grep -v bash | awk '{print $2}' \
		| while read -r pid; do kill -9 "$pid" 2>/dev/null; done || true
}

case "$1" in
	build)
		cd "$ROOT/ScriptureGuide"
		make
		# Embeds every locales/*.catkeys translation directly into the
		# binary's own resources (linkcatkeys, via the makefile-engine's
		# bindcatalogs target) -- a plain `make` only builds the binary,
		# it never runs this on its own, which is why menu items showed
		# up English even under a non-English system locale despite
		# translations existing in locales/de.catkeys etc.
		make bindcatalogs
		;;

	run)
		kill_matching ScriptureGuide
		cd "$ROOT/App"
		# nohup + disown so the app survives past this ssh session closing
		# (which otherwise sends SIGHUP to it) -- run/shot/log are separate
		# ssh calls, unlike a single long-lived interactive session.
		nohup ./ScriptureGuide > "$LOG" 2>&1 < /dev/null &
		disown
		;;

	shot)
		kill_matching screen_blanker
		# screenshot's exit code isn't a pass/fail signal (seen nonzero on
		# a normal, successful capture) -- the file on disk is the truth.
		screenshot -s -f png "$SHOT" || true
		;;

	log)
		cat "$LOG"
		;;

	kill)
		kill_matching ScriptureGuide
		kill_matching screen_blanker
		;;

	*)
		echo "usage: dev.sh {build|run|shot|log|kill}" >&2
		exit 1
		;;
esac
