#!/bin/sh
# Runs the host build in a folder laid out like the card, under fbshim's fake panel, for SECONDS
# (default 10), sending the datagrams in $INPUTS (a file, one "AT_SECONDS DATAGRAM" a line) to
# the input socket. Leaves test/out/last.png (the last frame shown) and the card folder in $CARD.
set -e
cd "$(dirname "$0")/.."
CARD=${CARD:-/tmp/q2cc-card}
SECONDS_RUN=${SECONDS_RUN:-10}
mkdir -p $CARD/.classicube/texpacks test/out
# the tests read the FPS and position lines (Show FPS, off by default on the Q2): a fresh card's
# options.txt turns it on, with the 1.9 defaults otherwise as they are
[ -f $CARD/.classicube/options.txt ] || printf "q2-defaults=True\nq2-defaults2=True\nq2-defaults3=True\nviewdist=80\ngui-showfps=True\n" > $CARD/.classicube/options.txt
cp ${BIN:-build/host/q2-classicube} $CARD/.classicube/q2-classicube
# TEXPACK=none: no texpacks/default.zip; TEXPACK=damaged: a cut-off copy
case "${TEXPACK:-}" in
  none)    rm -f $CARD/.classicube/texpacks/default.zip ;;
  damaged) head -c 30000 classicube/misc/cc_textures.zip > $CARD/.classicube/texpacks/default.zip ;;
  *)       cp classicube/misc/cc_textures.zip $CARD/.classicube/texpacks/default.zip ;;
esac
mkdir -p $CARD/.classicube/audio
cp classicube/misc/cc_audio.zip $CARD/.classicube/audio/default.zip
rm -f /tmp/q2-classicube.input.sock test/out/frame.raw
SND_OUT=$PWD/test/out/sound.raw FB_DUMP=$PWD/test/out/frame.raw FB_FINAL=$PWD/test/out/final.txt LD_PRELOAD=$PWD/build/host/fbshim.so \
    $CARD/.classicube/q2-classicube ${GAME_ARGS-plughw:0,0} > test/out/stdout.txt 2>&1 &
P=$!
python3 test/send.py ${INPUTS:-/dev/null} $SECONDS_RUN
kill -TERM $P 2>/dev/null || true
wait $P; echo "exit status $?"
cat test/out/final.txt
[ -s test/out/frame.raw ] && python3 test/view.py test/out/frame.raw test/out/last.png || true
