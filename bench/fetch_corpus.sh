#!/bin/sh
# Fetch a real-map benchmark corpus into bench/corpus/.
#
# Singles come from osu!'s raw .osu endpoint; The Unforgiving (a ranked
# 2012 marathon album set with 13 diffs and dense timing points) comes
# from the catboy.best mirror. ~1 MB total.
set -e
dir="$(dirname "$0")/corpus"
mkdir -p "$dir"
cd "$dir"

fetch() {
    [ -s "$2.osu" ] && return 0
    curl -sf --max-time 30 "https://osu.ppy.sh/osu/$1" -o "$2.osu"
    echo "fetched $2.osu"
}

fetch 75     "disco-prince-normal"          # first ranked map, format v3
fetch 129891 "freedom-dive-four-dimensions" # v9
fetch 131891 "big-black"                    # v9
fetch 658127 "blue-zenith-four-dimensions"  # v14

if ! ls "Within Temptation"*.osu >/dev/null 2>&1; then
    curl -sfL --max-time 120 "https://catboy.best/d/29157" -o unforgiving.osz
    unzip -o -q unforgiving.osz "*.osu"
    rm -f unforgiving.osz
    echo "fetched The Unforgiving (13 diffs)"
fi

echo "corpus ready: $(ls *.osu | wc -l | tr -d ' ') files in $dir"
echo "run: make bench BENCH_ARGS=bench/corpus"
