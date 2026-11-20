#!/bin/sh
# Builds q2-classicube for the Shanling Q2 (Ingenic X2000: MIPS32r2 little-endian, o32 with FR=1
# and NaN2008, glibc 2.28, as Q2 Pod's own helpers are built) and lays out the card folder.
#
#   ./build.sh   -> build/q2-classicube and dist/.classicube/ (copy .classicube to the card's root)
#
# Needs clang, ld.lld and python3, no MIPS sysroot: sysroot/include declares what's used, and the
# stub libraries tools/stubs.py makes carry the real sonames, so on the Q2 the rootfs's own glibc
# and libm are what run. ClassiCube is in classicube/ (see CLASSICUBE_COMMIT); its Core.h has a
# PLAT_Q2 block, and q2/ holds the Q2 window and platform.
set -e
cd "$(dirname "$0")"

# OPT: the optimisation (-O3 from 1.19, after 1.18's -O3 build ran well on the Q2; OPT=-O2 ./build.sh
# builds into build-O2/ and dist-O2/)
OPT=${OPT:--O3}
B=build; D=dist
[ "$OPT" = "-O3" ] || { B=build-${OPT#-}; D=dist-${OPT#-}; }
TARGET="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008"
# -G0: no $gp-relative small data. glibc calls main with its own $gp (libc is PIC), so a
# gp-relative global in our non-PIC code would land in libc's memory.
CFLAGS="$TARGET -mabicalls -fno-pic -G0 -nostdlibinc -isystem sysroot/include $OPT -DQ2_OPT=\"$OPT\" \
        -fno-strict-aliasing -fno-math-errno -ffunction-sections -fdata-sections -fno-stack-protector \
        -fno-unwind-tables -fno-asynchronous-unwind-tables -DPLAT_Q2 -Iclassicube/src -Iq2"
OURS="Platform_Q2 Window_Q2 Audio_Q2 start rt"
STRICT="-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-function -Werror"

mkdir -p $B/obj
python3 tools/stubs.py $B/stubs

# A header (or build.sh) changed since the last build: every object is rebuilt, as any of them may
# include it. The per-file test below only sees each source's own changes.
if [ ! -f $B/obj/.headers ] || [ -n "$(find q2 classicube/src build.sh \( -name '*.h' -o -name '*.i' -o -name build.sh \) -newer $B/obj/.headers | head -1)" ]; then
    rm -f $B/obj/*.o
fi
touch $B/obj/.headers

for src in classicube/src/*.c q2/*.c; do
    name=$(basename "$src" .c)
    obj=$B/obj/$name.o
    flags="-w"
    case " $OURS " in *" $name "*) flags="$STRICT" ;; esac
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        clang $CFLAGS $flags -c "$src" -o "$obj"
    fi
done

# linked with its symbols (build/q2-classicube.debug, to look up a crash's addresses in the log with
# llvm-addr2line -f -e build/q2-classicube.debug ADDRESS), then stripped for the card
ld.lld -m elf32ltsmip -e __start --dynamic-linker /lib/ld-linux-mipsn8.so.1 \
    --image-base=0x400000 -z noexecstack --gc-sections --hash-style=sysv --build-id=none \
    --export-dynamic-symbol=_IO_stdin_used \
    --as-needed $B/obj/*.o $B/stubs/libpthread.so.0 $B/stubs/libasound.so.2 $B/stubs/libc.so.6 -o $B/q2-classicube.debug
llvm-objcopy --strip-all $B/q2-classicube.debug $B/q2-classicube

rm -rf $D
mkdir -p $D/.classicube/texpacks
cp $B/q2-classicube $D/.classicube/q2-classicube
cp card/q2app card/icon.png card/icon-40.png $D/.classicube/
cp classicube/misc/cc_textures.zip $D/.classicube/texpacks/default.zip
mkdir -p $D/.classicube/audio
cp classicube/misc/cc_audio.zip $D/.classicube/audio/default.zip
cp card/README.txt $D/.classicube/README.txt
cp classicube/license.txt $D/.classicube/LICENSE-ClassiCube.txt
echo "built: $B/q2-classicube ($(wc -c < $B/q2-classicube) bytes), card folder: $D/.classicube"
