#!/bin/sh
# test/count-one.sh BIN NAME "OPTS": one build under callgrind for test/count.sh (instrumentation off
# until callgrind_control turns it on); /tmp/cg.NAME
cd "$(dirname "$0")/.."
n=$2
mkdir -p /tmp/out-$n
sed "s#test/out#/tmp/out-$n#g; s#\$PWD//tmp#/tmp#g" test/run-host.sh > test/.run-host-$n.sh; chmod +x test/.run-host-$n.sh
cat > /tmp/vgwrap-$n.sh <<W
#!/bin/sh
d=\$(dirname "\$0")
cp $1 \$d/q2-real
exec valgrind --tool=callgrind --instr-atstart=no --cache-sim=yes --I1=32768,8,32 --D1=32768,8,32 --LL=524288,8,32 --callgrind-out-file=/tmp/cg.$n \$d/q2-real "\$@"
W
chmod +x /tmp/vgwrap-$n.sh
CARD=/tmp/q2cc-vg-$n; rm -rf $CARD && mkdir -p $CARD/.classicube/maps
cp ${WORLD:-test/bench-world.cw} $CARD/.classicube/maps/q2-world.cw
printf "q2-defaults=True\nq2-defaults2=True\nq2-defaults3=True\nq2-defaults4=True\nq2-defaults5=True\nviewdist=45\nq2-cores=1\nq2-pace=false\n$3\n" > $CARD/.classicube/options.txt
BIN=/tmp/vgwrap-$n.sh INPUTS=${INPUTS:-/dev/null} FB_DUMP_EVERY=1000000 CARD=$CARD SECONDS_RUN=${TOTAL:-150} test/.run-host-$n.sh > /tmp/vg-$n.out 2>&1
rm -f test/.run-host-$n.sh
