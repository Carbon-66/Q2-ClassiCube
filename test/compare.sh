#!/bin/sh
# Frame rates of two or more host builds, side by side: frames drawn in 8 s per view, two runs each,
# flying (the camera holds still). Each binary as NAME=PATH (a host build, test/host-build.sh; an
# older one from a git worktree). The views beyond "bench" are the worlds test/occlusion.sh and the
# water checks make in /tmp/occ. Options in $OPTS.
#   test/compare.sh old=/tmp/old/build/host/q2-classicube new=build/host/q2-classicube
cd "$(dirname "$0")/.."
VIEWS="${VIEWS:-bench spawn-e down high_h32 high_h64 high_h96 high2_h0 high2_h128}"
[ -f /tmp/flyon.txt ] || printf "0.6 k d 218\n0.65 k u 218\n0.75 k d 218\n0.8 k u 218\n" > /tmp/flyon.txt
for v in $VIEWS; do
  w=/tmp/occ/$v.cw; [ $v = bench ] && w=test/bench-world.cw
  [ -f $w ] || { echo "$v: no world ($w)"; continue; }
  line="$v:"
  for k in 1 2; do
    for nb in "$@"; do
      n=${nb%%=*}; b=${nb#*=}
      f=$(BIN=$b WORLD=$w INPUTS=/tmp/flyon.txt SECONDS=8 BENCH_OPTS="q2-defaults4=True
q2-defaults5=True
viewdist=45
$OPTS" test/bench.sh)
      line="$line $n=$f"
    done
  done
  echo "$line"
done
