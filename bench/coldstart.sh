#!/bin/sh
# Cold-start corpus driver: one fresh process per (file, rep), pinned to a
# core on Linux. Reports the corpus aggregate (bytes / summed per-file best
# times) for the cold first parse and for the warm best-of-8, the median
# cold/warm ratio, page faults, and a per-file table sorted by size.
#
#   sh bench/coldstart.sh "<bin>" <dir> [reps=5] [cpu=]
#
# <bin> may be several words ("arch -x86_64 ./build/coldstart_x86").
# FOSU_PERF=1 in the environment adds hardware counters (perf permissions).
set -e
BIN=$1
DIR=$2
REPS=${3:-5}
CPU=$4
[ -n "$BIN" ] && [ -d "$DIR" ] || {
    echo "usage: coldstart.sh \"<bin>\" <dir> [reps] [cpu]" >&2
    exit 2
}
PIN=
if [ -n "$CPU" ] && command -v taskset >/dev/null 2>&1; then
    PIN="taskset -c $CPU"
fi

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
for f in "$DIR"/*.osu; do
    i=0
    while [ "$i" -lt "$REPS" ]; do
        # shellcheck disable=SC2086
        line=$($PIN $BIN "$f") || exit 1
        printf '%s\t%s\n' "$(basename "$f")" "$line" >> "$tmp"
        i=$((i + 1))
    done
done

# Columns: 1 name 2 bytes 3 objects 4 read_ns 5 cold_ns 6 cold_flt
#          7 warm_ns 8 warm_flt 9-15 cold counters 16-22 warm counters
awk -F'\t' '
{
    n = $1
    if (!(n in bytes)) { order[++nf] = n; bytes[n] = $2; objs[n] = $3
        cold[n] = $5; warm[n] = $7; rd[n] = $4; cflt[n] = $6; wflt[n] = $8
        for (k = 9; k <= 22; k++) ctr[n, k] = $k }
    else {
        if ($5 < cold[n]) { cold[n] = $5; cflt[n] = $6
            for (k = 9; k <= 15; k++) ctr[n, k] = $k }
        if ($7 < warm[n]) { warm[n] = $7; wflt[n] = $8
            for (k = 16; k <= 22; k++) ctr[n, k] = $k }
        if ($4 < rd[n]) rd[n] = $4
    }
}
END {
    tb = 0; tc = 0; tw = 0; tr = 0; tcf = 0; twf = 0; to = 0
    for (i = 1; i <= nf; i++) { n = order[i]
        tb += bytes[n]; tc += cold[n]; tw += warm[n]; tr += rd[n]
        tcf += cflt[n]; twf += wflt[n]; to += objs[n]
        ratio[i] = cold[n] / warm[n]
        for (k = 9; k <= 22; k++) tctr[k] += ctr[n, k]
    }
    asort(ratio)
    med = (nf % 2) ? ratio[(nf + 1) / 2] : (ratio[nf / 2] + ratio[nf / 2 + 1]) / 2
    printf "files %d   bytes %.2f MB   objects %d   reps/file: best-of\n", nf, tb / 1e6, to
    printf "%-28s %9.1f MB/s  %8.1f us/file  %6.1f faults/file\n", "cold (fresh process, 1st)", tb / tc * 1e3, tc / nf / 1e3, tcf / nf
    printf "%-28s %9.1f MB/s  %8.1f us/file  %6.1f faults/file\n", "warm (same process, best-of)", tb / tw * 1e3, tw / nf / 1e3, twf / nf
    printf "%-28s %9.1f MB/s  %8.1f us/file\n", "read() (cold page cache?)", tb / tr * 1e3, tr / nf / 1e3
    printf "cold/warm: aggregate %.2fx   median per-file %.2fx\n", tc / tw, med
    if (tctr[9] > 0) {
        split("cycles instructions branch_misses l1i_misses itlb_misses dtlb_misses l1d_misses", nm, " ")
        printf "\n%-16s %14s %14s %10s\n", "per file", "cold", "warm", "cold-warm"
        for (k = 0; k < 7; k++)
            printf "%-16s %14.0f %14.0f %10.0f\n", nm[k + 1], tctr[9 + k] / nf, tctr[16 + k] / nf, (tctr[9 + k] - tctr[16 + k]) / nf
        printf "%-16s %14.2f %14.2f\n", "IPC", tctr[10] / tctr[9], tctr[17] / tctr[16]
    }
    printf "\n%-34s %8s %7s %9s %9s %6s %5s %5s\n", "file", "bytes", "objs", "cold_us", "warm_us", "ratio", "cflt", "wflt"
    # sort by bytes
    for (i = 1; i <= nf; i++) idx[i] = i
    for (i = 2; i <= nf; i++) { v = idx[i]; j = i - 1
        while (j >= 1 && bytes[order[idx[j]]] < bytes[order[v]]) { idx[j + 1] = idx[j]; j-- }
        idx[j + 1] = v }
    for (i = 1; i <= nf; i++) { n = order[idx[i]]
        printf "%-34.34s %8d %7d %9.1f %9.1f %6.2f %5d %5d\n", n, bytes[n], objs[n], cold[n] / 1e3, warm[n] / 1e3, cold[n] / warm[n], cflt[n], wflt[n] }
}' "$tmp"
