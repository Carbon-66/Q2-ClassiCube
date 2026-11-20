#!/bin/sh
# q2-classicube built for this machine (the same sources and PLAT_Q2 config, the host's own libc),
# for test/run.sh: build/host/q2-classicube and build/host/fbshim.so.
set -e
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p $OUT/obj
# a header changed since the last build: rebuild every object (see build.sh)
if [ ! -f $OUT/obj/.headers ] || [ -n "$(find q2 classicube/src test/host-build.sh \( -name '*.h' -o -name '*.i' -o -name host-build.sh \) -newer $OUT/obj/.headers | head -1)" ]; then
    rm -f $OUT/obj/*.o
fi
touch $OUT/obj/.headers
CF="-O1 -g -fno-strict-aliasing -fno-math-errno -DPLAT_Q2 -D_GNU_SOURCE -Iclassicube/src -Iq2"
for src in classicube/src/*.c q2/Platform_Q2.c q2/Window_Q2.c q2/Audio_Q2.c test/alsa_fake.c; do
    name=$(basename $src .c)
    obj=$OUT/obj/$name.o
    [ -f $obj ] && [ $obj -nt $src ] || clang $CF -w -c $src -o $obj
done
clang -pthread $OUT/obj/*.o -o $OUT/q2-classicube -lm
clang -shared -fPIC -O1 test/fbshim.c -o $OUT/fbshim.so -ldl
echo "host build: $OUT/q2-classicube"
