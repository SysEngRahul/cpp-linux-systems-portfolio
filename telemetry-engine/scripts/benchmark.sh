#!/usr/bin/env bash
# Sweep worker counts x queue type x aggregation type and write a CSV.
#
# Tunables (environment variables):
#   RECORDS=5M        records per run
#   PRODUCERS=2       producer threads
#   WORKERS_LIST="1 2 4 8"
#   CAPACITY=65536
#   DEVICES=1024
#   REPEAT=3          runs per configuration (median-able in the CSV)
#   PIN=0             set to 1 to pin threads to cores
#   BIN=./build/telemetry_engine
#   OUT=results/bench_<timestamp>.csv
set -euo pipefail
cd "$(dirname "$0")/.."

BIN="${BIN:-./build/telemetry_engine}"
RECORDS="${RECORDS:-5M}"
PRODUCERS="${PRODUCERS:-2}"
WORKERS_LIST="${WORKERS_LIST:-1 2 4 8}"
CAPACITY="${CAPACITY:-65536}"
DEVICES="${DEVICES:-1024}"
REPEAT="${REPEAT:-3}"
PIN="${PIN:-0}"
OUT="${OUT:-results/bench_$(date +%Y%m%d_%H%M%S).csv}"

[[ -x "$BIN" ]] || { echo "error: $BIN not found - run ./scripts/build.sh first"; exit 1; }
mkdir -p "$(dirname "$OUT")"

extra=()
[[ "$PIN" == "1" ]] && extra+=(--pin)

first=1
for w in $WORKERS_LIST; do
  for q in mutex lockfree; do
    for a in local mutex atomic; do
      for ((i = 1; i <= REPEAT; i++)); do
        hdr=()
        (( first )) && hdr+=(--csv-header) && first=0
        echo ">> workers=$w queue=$q agg=$a run=$i/$REPEAT" >&2
        "$BIN" --csv "${hdr[@]}" -n "$RECORDS" -p "$PRODUCERS" -w "$w" \
               -c "$CAPACITY" -d "$DEVICES" -q "$q" -a "$a" --verify "${extra[@]}" >> "$OUT"
      done
    done
  done
done

echo
echo "Wrote $OUT"
echo
# Compact view: queue, agg, workers, throughput, p99 latency, cpu%
awk -F, 'NR==1{next} {printf "%-9s %-7s w=%-3s %8.2f Mrec/s  p99=%9.1f us  cpu=%6.1f%%  rss=%5.1f MB  %s\n",$1,$2,$4,$10,$16,$19,$22,$23}' "$OUT" | sort -k1,1 -k2,2 -k3,3V
