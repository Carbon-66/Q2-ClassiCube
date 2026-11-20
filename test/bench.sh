#!/bin/sh
# Frames drawn in SECONDS (default 10) on a fixed world (test/bench-world.cw) and view, no input,
# by the host build (test/host-build.sh), with CORES (q2-cores, default 2) drawing threads. Prints
# the frame count. Swaps aren't paced to the panel's refresh here (q2-pace=false). Frames aren't written out (FB_DUMP_EVERY), which would weigh on the main thread.
set -e
cd "$(dirname "$0")/.."
CARD=/tmp/q2cc-bench
rm -rf $CARD && mkdir -p $CARD/.classicube/maps
cp ${WORLD:-test/bench-world.cw} $CARD/.classicube/maps/q2-world.cw
printf "q2-defaults=True\nq2-defaults2=True\nq2-defaults3=True\nq2-defaults4=True\nq2-defaults5=True\nviewdist=45\nq2-cores=${CORES:-2}\nq2-pace=false\n${BENCH_OPTS:-}\n" > $CARD/.classicube/options.txt
INPUTS=${INPUTS:-/dev/null} FB_DUMP_EVERY=1000000 CARD=$CARD SECONDS_RUN=${SECONDS:-10} test/run-host.sh > /dev/null 2>&1 || true
sed 's/.*pans //' test/out/final.txt
