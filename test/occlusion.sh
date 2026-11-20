#!/bin/sh
# Occlusion culling check (q2-occlusion, Graphics_SoftGPU.c): for each view (a copy of the test world whose spawn is moved), a frame
# with q2-occlusion on and one with it off, compared pixel for pixel (the FPS line hidden).
set -e
cd "$(dirname "$0")/.."
mkdir -p /tmp/occ
python3 - <<'PY'
import gzip, struct
d = gzip.open('test/bench-world.cw').read()
i = d.find(b'Spawn')
def spawn(x, y, z, h, p):
    b = bytearray(d)
    for k, v in zip(b'XYZ', (x, y, z)):
        j = b.find(b'\x02\x00\x01' + bytes([k]), i); b[j+4:j+6] = struct.pack('>h', v)
    for k, v in zip(b'HP', (h, p)):
        j = b.find(b'\x01\x00\x01' + bytes([k]), i); b[j+4] = v
    return bytes(b)
views = {'spawn': (64,37,64,0,0), 'spawn-e': (64,37,64,64,0), 'spawn-s': (64,37,64,128,0),
         'spawn-w': (64,37,64,192,0), 'down': (64,37,64,0,40), 'up': (64,37,64,0,216),
         'buried': (64,15,64,32,0), 'corner': (6,50,6,224,10), 'edge': (120,50,64,64,20),
         'cave': (40,20,90,100,0)}
for n, v in views.items():
    gzip.open(f'/tmp/occ/{n}.cw', 'wb').write(spawn(*v))
open('/tmp/occ/views.txt', 'w').write(' '.join(views))
PY
echo "4.0 snap occ" > /tmp/occ/in.txt
fail=0
for v in $(cat /tmp/occ/views.txt); do
  for o in true false; do
    rm -rf /tmp/occ/card && mkdir -p /tmp/occ/card/.classicube/maps
    cp /tmp/occ/$v.cw /tmp/occ/card/.classicube/maps/q2-world.cw
    printf "q2-occlusion=$o\ngui-showfps=false\n${OCC_OPTS:-}\n" > /tmp/occ/card/.classicube/options.txt
    CARD=/tmp/occ/card INPUTS=/tmp/occ/in.txt SECONDS_RUN=4.5 test/run-host.sh >/dev/null 2>&1
    cp test/out/occ.png /tmp/occ/$v-$o.png
  done
  r=$(python3 -c "
from PIL import Image
a=Image.open('/tmp/occ/$v-true.png').convert('RGB').tobytes(); b=Image.open('/tmp/occ/$v-false.png').convert('RGB').tobytes()
print(sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3]))")
  echo "$v: $r pixels differ"; [ "$r" = 0 ] || fail=1
done
[ $fail = 0 ] && echo "occlusion: ok"
exit $fail
