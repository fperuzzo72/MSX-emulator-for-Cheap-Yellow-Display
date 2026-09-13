#!/bin/sh
# Turn tapes into snapshots, so the board never has to wait for one.
#
#   tools/tapes_to_snaps.sh 48.rom roms/spectrum roms/spectrum-snaps
#
# For each .tap it boots a Spectrum on THIS machine, types LOAD "", waits
# for the load, and writes the loaded memory out as a .sna - then puts the
# .sna back into a fresh machine to check the game is really in there. A
# snapshot that does not come back is deleted rather than shipped.
#
# This is the answer to tape loading being slow. A game whose own loader
# reads the tape takes minutes on the board, because that is how long the
# tape is; converted once here, it starts instantly and forever.
#
# Neither tapes nor snapshots are distributed with this project.
set -e

ROM=$1
TAPES=$2
OUT=$3
[ -n "$OUT" ] || { sed -n '2,12p' "$0"; exit 2; }

cd "$(dirname "$0")/.."
make -s -C tools/tapebench
mkdir -p "$OUT"

ok=0
failed=""
for tap in "$TAPES"/*.tap; do
    name=$(basename "$tap" .tap)
    if ./tools/tapebench/load "$ROM" "$tap" 40000 "$OUT/$name.sna" >/dev/null 2>&1; then
        ok=$((ok + 1))
        printf '  %s\n' "$name"
    else
        failed="$failed$name, "
    fi
done

echo "$ok snapshot(s) in $OUT"
[ -z "$failed" ] || echo "did not convert: $failed(keep their tapes)"
