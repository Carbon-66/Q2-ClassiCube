#!/bin/sh
# test/count.sh NAME1 BIN1 OPTS1 NAME2 BIN2 OPTS2: instructions and cache misses a frame of two host builds
# (or option sets) on the bench view, counted by callgrind (valgrind) from 110 s to 185 s, once
# the world is built; both at once (the counts don't depend on the timing), no input. One core
# (q2-cores=1). The X2000 runs instructions in order, so the count is a steadier guide to it than
# the host's frame rate. Takes about 3 minutes.
#   test/count.sh old /tmp/old/build/host/q2-classicube "" new build/host/q2-classicube "q2-renderscale=75"
cd "$(dirname "$0")/.."
(TOTAL=185 test/count-one.sh "$2" "$1" "$3" &); sleep 1; (TOTAL=185 test/count-one.sh "$5" "$4" "$6" &); sleep 110; callgrind_control -i on >/dev/null; sleep 82
python3 - "$1" "$4" <<'P'
import re, sys
for n in sys.argv[1:]:
    raw=open('/tmp/cg.'+n).read()
    names={}
    for m in re.finditer(r'^c?fn=\((\d+)\) (\S+)', raw, re.M): names[m.group(1)]=m.group(2)
    calls={}
    for m in re.finditer(r'^cfn=\((\d+)\).*\ncalls=(\d+)', raw, re.M):
        k=names.get(m.group(1)); calls[k]=calls.get(k,0)+int(m.group(2))
    f=calls.get('Gfx_EndFrame',1)
    tot=re.search(r'^(?:summary|totals): (.*)$', raw, re.M).group(1).split()
    ev=re.search(r'^events: (.*)$', raw, re.M).group(1).split()
    d=dict(zip(ev,map(int,tot)))
    print('%-8s frames %4d  Ir/frame %6.2fM  D1 misses/frame %6.0fk  LL misses/frame %6.0fk  tris/frame %d' % (n, f, d['Ir']/f/1e6, (d['D1mr']+d['D1mw'])/f/1e3, (d['DLmr']+d['DLmw'])/f/1e3, calls.get('DrawTriangle3D',0)/f))
P
