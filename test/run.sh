#!/bin/sh
# Host tests: q2-classicube built for this machine (test/host-build.sh) and run under fbshim's fake
# Q2 panel in a folder laid out like the card. Checks that it draws, takes Q2 Pod's input, leaves
# on a TERM (Return held) and on Quit game with the world saved and the panel back on Q2 Pod's
# page, and loads the world again at the next start. Frames land in test/out/*.png.
set -e
cd "$(dirname "$0")/.."
test/host-build.sh
CARD=/tmp/q2cc-test
SOCK=/tmp/q2-classicube.input.sock
fail() { echo "FAIL: $*"; exit 1; }

echo "== first start, a short play session, TERM"
rm -rf $CARD
CARD=$CARD INPUTS=test/play.txt SECONDS_RUN=12 test/run-host.sh | tee test/out/run1.txt
grep -q "exit status 0" test/out/run1.txt || fail "exit status"
pans=$(sed 's/.*pans //' test/out/final.txt)
[ "$pans" -gt 50 ] || fail "only $pans frames"
grep -q "yoffset 0 " test/out/final.txt || fail "panel not back on Q2 Pod's page"
[ ! -e $SOCK ] || fail "input socket left behind"
[ -s $CARD/.classicube/maps/q2-world.cw ] || fail "no world saved"
grep -q "World saved" $CARD/.classicube/classicube.log || fail "no save in the log"
grep -q "Error" $CARD/.classicube/classicube.log && fail "errors in the log"
for f in p1-walking p3-wheel p6-menu; do [ -s test/out/$f.png ] || fail "no $f frame"; done
grep -q "Sound: plughw:0,0 at 44100 Hz" $CARD/.classicube/classicube.log || fail "no sound output opened"
python3 - <<'PY' || fail "no sound heard"
import struct
b = open('test/out/sound.raw', 'rb').read()
n = len(b) // 4
s = struct.unpack(f'<{n * 2}h', b[:n * 4])
assert n > 44100 * 8, f'only {n} frames of sound in 12 s'
# the world is new each run (footsteps or not, in water or on grass): some sound, and no hard clip
peak = max(abs(x) for x in s)
assert peak > 500, 'silent all through the play session'
assert peak < 32767, 'the mix clipped'
PY
python3 - <<'PY' || fail "input had no effect"
from PIL import ImageChops, Image
def diff(a, b):
    return ImageChops.difference(Image.open(f'test/out/{a}.png').convert('RGB'), Image.open(f'test/out/{b}.png').convert('RGB')).getbbox() is not None
assert diff('p0-start', 'p1-walking'), 'walking changed nothing'
assert diff('p2-looked', 'p3-wheel'), 'the wheel changed nothing'
assert diff('p5-placed', 'p6-menu'), 'Return opened no menu'
PY
echo "first start: ok ($pans frames)"

echo "== second start: the world comes back; Quit game from the pause menu"
cp $CARD/.classicube/maps/q2-world.cw /tmp/q2cc-world-before
rm -f $SOCK
LD_PRELOAD=$PWD/build/host/fbshim.so FB_FINAL=$PWD/test/out/final.txt $CARD/.classicube/q2-classicube & P=$!
python3 test/send.py test/quit.txt 5 2>/dev/null || true
sleep 1
kill -0 $P 2>/dev/null && { kill $P; fail "Quit game didn't end it"; }
wait $P || fail "exit status $?"
grep -q "Loading maps/q2-world.cw" $CARD/.classicube/classicube.log || fail "world not loaded"
grep -q "yoffset 0 " test/out/final.txt || fail "panel not back on Q2 Pod's page"
[ ! -e $SOCK ] || fail "input socket left behind"
echo "second start: ok"
echo "== the numpad with the wheel and Centre: View distance 80 -> 16, then its Enter"
rm -rf $CARD
CARD=$CARD INPUTS=test/kb.txt SECONDS_RUN=11 test/run-host.sh > test/out/run3.txt
grep -q "^viewdist=16" $CARD/.classicube/options.txt || fail "view distance not typed and applied: $(grep viewdist $CARD/.classicube/options.txt)"
echo "numpad: ok"

echo "== the letter keyboard: 13 steps on from 1 is q"
rm -rf $CARD
CARD=$CARD INPUTS=test/text.txt SECONDS_RUN=7 test/run-host.sh > test/out/run4.txt
python3 - <<'PY' || fail "the wheel didn't reach q"
from PIL import Image, ImageStat
# the q key (second row, first column) is lit, w and e beside it aren't
im = Image.open('test/out/t2-row2.png').convert('L')
cell = lambda x: ImageStat.Stat(im.crop((x, 422, x + 4, 426))).mean[0]
q, w, e = cell(42), cell(84), cell(126)
assert q > w + 8 and q > e + 8, (q, w, e)
PY
echo "keyboard: ok"
echo "== flying with Centre and Play/Pause, the hotbar with Next and Previous"
rm -rf $CARD
CARD=$CARD INPUTS=test/fly.txt SECONDS_RUN=9 test/run-host.sh > test/out/run5.txt
python3 - <<'PY' || fail "the media buttons did nothing"
from PIL import Image, ImageChops
def same(a, b, box):
    pa = Image.open(f'test/out/{a}.png').convert('RGB').crop(box)
    pb = Image.open(f'test/out/{b}.png').convert('RGB').crop(box)
    return ImageChops.difference(pa, pb).getbbox() is None
pos = (40, 85, 400, 125)          # the "Position: (x,y,z)" line
hotbar = (100, 560, 650, 640)
assert not same('f0-flying', 'f1-up', pos), 'Centre held: no climb'
assert not same('f1-up', 'f2-down', pos), 'Play/Pause held: no descent'
assert not same('f2-down', 'f3-next2', hotbar), 'Next: the hotbar did not move'
assert not same('f3-next2', 'f4-prev1', hotbar), 'Previous: the hotbar did not move'
PY
echo "buttons: ok"
echo "== Next with its releases lost: each press shows at once; Play/Pause as quick pulses"
rm -rf $CARD
CARD=$CARD INPUTS=test/lostup.txt SECONDS_RUN=9 test/run-host.sh > test/out/run6.txt
rm -rf $CARD
CARD=$CARD INPUTS=test/flypulse.txt SECONDS_RUN=8.5 test/run-host.sh > test/out/run7.txt
python3 - <<'PY' || fail "a press was lost or late"
from PIL import Image, ImageChops
def same(a, b, box):
    pa = Image.open(f'test/out/{a}.png').convert('RGB').crop(box)
    pb = Image.open(f'test/out/{b}.png').convert('RGB').crop(box)
    return ImageChops.difference(pa, pb).getbbox() is None
hotbar, pos = (100, 560, 650, 640), (40, 85, 400, 125)
assert not same('l0', 'l1', hotbar), 'the first Next did not show'
assert not same('l1', 'l2', hotbar), 'Next after a lost release did nothing'
assert not same('p0-up', 'p1-onepulse', pos), 'one Play/Pause pulse: no descent'
assert not same('p1-onepulse', 'p2-pulses', pos), 'repeating pulses: no descent'
PY
echo "presses: ok"

echo "== Centre pressed twice quickly: flying on (and up while held), twice again: off"
rm -rf $CARD
CARD=$CARD INPUTS=test/dbljump.txt SECONDS_RUN=10 test/run-host.sh > test/out/run9.txt
python3 - <<'PY' || fail "double jump"
from PIL import Image, ImageChops
def same(a, b, box):
    pa = Image.open(f'test/out/{a}.png').convert('RGB').crop(box)
    pb = Image.open(f'test/out/{b}.png').convert('RGB').crop(box)
    return ImageChops.difference(pa, pb).getbbox() is None
pos = (40, 85, 400, 125)
assert not same('d0-start', 'd1-flying', pos), 'no climb after a double press'
assert same('d1-flying', 'd1b-still', pos), 'not flying: fell after the climb'
assert not same('d1b-still', 'd2-off', pos), 'a second double press did not stop flying'
assert same('d2-off', 'd3-slow', pos), 'two slow presses toggled flying'
PY
echo "double jump: ok"

echo "== without q2app's audio=1 (no arguments): the game says so"
rm -rf $CARD
CARD=$CARD GAME_ARGS= INPUTS=/dev/null SECONDS_RUN=5 test/run-host.sh > test/out/run8.txt
grep -q "no audio=1" $CARD/.classicube/classicube.log || fail "no warning without audio=1"
echo "no audio=1: ok"
echo "== texpacks/default.zip left out of the card, or damaged: the textures built in"
for t in none damaged; do
  rm -rf $CARD
  CARD=$CARD TEXPACK=$t INPUTS=/dev/null SECONDS_RUN=4 test/run-host.sh > test/out/run10.txt
  grep -q "using the textures built in" $CARD/.classicube/classicube.log || fail "$t texture pack: built-in textures not used"
  grep -q "Missing file" $CARD/.classicube/classicube.log && fail "$t texture pack: the missing-file warning"
  grep -q "Loaded terrain atlas: 1 bmps, 256 per bmp" $CARD/.classicube/classicube.log || fail "$t texture pack: no 256-tile atlas"
done
echo "built-in textures: ok"
echo "== a release right behind a burst of moves, sent as Q2 Pod sends: the stick lets go"
for k in 1 2 3; do
  rm -rf $CARD; mkdir -p $CARD/.classicube/maps; cp test/bench-world.cw $CARD/.classicube/maps/q2-world.cw
  printf "q2-defaults=True\nq2-defaults2=True\nq2-defaults3=True\nq2-defaults4=True\nq2-defaults5=True\nviewdist=45\ngui-showfps=false\nfpslimit=Limit30FPS\n" > $CARD/.classicube/options.txt
  Q2POD_DROP=1 CARD=$CARD INPUTS=test/stuck.txt SECONDS_RUN=4 test/run-host.sh > test/out/run11.txt
  d=$(python3 -c "
from PIL import Image
a=Image.open('test/out/stuckA.png').convert('RGB').crop((0,0,750,520)).tobytes(); b=Image.open('test/out/stuckB.png').convert('RGB').crop((0,0,750,520)).tobytes()
print(sum(1 for i in range(0,len(a),3) if a[i:i+3]!=b[i:i+3]))")
  [ $d -gt 20000 ] && fail "the thumbstick stayed held after its release (try $k: $d pixels moved)"
done
echo "release after a burst: ok"
echo "== the touchscreen device says lifted, the release never came: let go; resting and Centre: held"
rm -rf $CARD; mkdir -p $CARD/.classicube/maps; cp test/bench-world.cw $CARD/.classicube/maps/q2-world.cw
printf "q2-defaults=True\nq2-defaults2=True\nq2-defaults3=True\nq2-defaults4=True\nq2-defaults5=True\nviewdist=45\ngui-showfps=false\n" > $CARD/.classicube/options.txt
rm -f /tmp/q2-touchdev.fifo; mkfifo /tmp/q2-touchdev.fifo
Q2_TOUCHDEV=/tmp/q2-touchdev.fifo CARD=$CARD INPUTS=test/lift.txt SECONDS_RUN=8 test/run-host.sh > test/out/run12.txt
d=$(python3 -c "
from PIL import Image
a=Image.open('test/out/liftC.png').convert('RGB').crop((0,0,750,520)).tobytes(); b=Image.open('test/out/liftD.png').convert('RGB').crop((0,0,750,520)).tobytes()
print(sum(1 for i in range(0,len(a),3) if a[i:i+3]!=b[i:i+3]))")
[ $d -gt 20000 ] && fail "the stick stayed held after the screen's lift ($d pixels moved)"
grep -q "Touchscreen device /tmp/q2-touchdev.fifo" $CARD/.classicube/classicube.log || fail "the touchscreen device wasn't read"
grep -E "Key d 218|Touch let go" $CARD/.classicube/classicube.log | sed 's/ at .*//; s/:.*//' | tr '\n' '|' > test/out/lift-order.txt
[ "$(cat test/out/lift-order.txt)" = "Key d 218|Touch let go|" ] || fail "lift: the log's order was $(cat test/out/lift-order.txt)"
echo "lift from the device: ok"
echo "all ok"
