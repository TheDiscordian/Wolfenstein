#!/usr/bin/env bash
set -euo pipefail
# Pre-render all IMF music at native OPL quality into the standalone
# muscache.ofx pack (data slot 27).  The port looks songs up by content hash
# and falls back to on-device DBOPL synthesis, so the cache is optional &
# never stale.  The pack is large (~270 MB for Blake Stone) and derived from
# the game's own data, so it is generated next to the game files and NEVER
# synced into dist/ or committed.
#
# Usage: scripts/muscache.sh <dir>
#   <dir> contains the game data (AUDIOHED.*/AUDIOT.*),
#   e.g. /run/media/<user>/<CARD>/Assets/blakestone/common
#
# STARTMUSIC overrides the first music chunk index (default 300, the
# bstone/Blake Stone AUDIOT layout: 0-99 PC speaker, 100-199 AdLib SFX,
# 200-299 digitized, 300+ music).

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="${1:?usage: muscache.sh <game-data dir>}"
STARTMUSIC="${STARTMUSIC:-300}"
TOOL="$ROOT/.obj/muscache"
SRC="$ROOT/src/tools/muscache/muscache.cpp"
DBOPL="$ROOT/src/wolfenstein/port/dosbox/dbopl.cpp"

mkdir -p "$ROOT/.obj"
if [ ! -x "$TOOL" ] || [ "$SRC" -nt "$TOOL" ] || [ "$DBOPL" -nt "$TOOL" ] || [ "$ROOT/src/tools/sfxcache/nuked/opl3.c" -nt "$TOOL" ]; then
    echo "muscache: building native tool..."
    g++ -O2 -DUSE_GPL \
        -I "$ROOT/src/tools/sfxcache/shim" \
        -I "$ROOT/src/tools/sfxcache/nuked" \
        -I "$ROOT/src/wolfenstein/port/dosbox" \
        -o "$TOOL" "$SRC" "$DBOPL" \
        "$ROOT/src/tools/sfxcache/nuked/opl3.c"
fi

# Collect AUDIOHED/AUDIOT pairs (any extension: BS6, VSI, ...)
pairs=()
shopt -s nullglob nocaseglob
for hed in "$DIR"/AUDIOHED.*; do
    ext="${hed##*.}"
    for t in "$DIR"/AUDIOT.*; do
        # case-insensitive: nocaseglob matches any case but [ = ] does not
        text="${t##*.}"
        [ "${text,,}" = "${ext,,}" ] && pairs+=("$hed" "$t")
    done
done
shopt -u nullglob nocaseglob
if [ ${#pairs[@]} -eq 0 ]; then
    echo "muscache: no AUDIOHED/AUDIOT pairs found in $DIR" >&2
    exit 1
fi

OFX="$DIR/muscache.ofx"
"$TOOL" "$OFX" "$STARTMUSIC" "${pairs[@]}"
