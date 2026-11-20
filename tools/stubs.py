#!/usr/bin/env python3
"""Stub shared libraries to link q2-classicube against. Each has the real library's soname and the
symbols q2-classicube imports, so lld writes the right NEEDED entries and dynamic relocations. On
the Q2, the rootfs's own glibc 2.28 resolves them. Usage: stubs.py OUTDIR

Symbol versions: an unversioned reference binds to a symbol's oldest version. On the Q2's MIPS
glibc fopen, fclose and pthread_create have an old GLIBC_2.0 compatibility version (LinuxThreads'
pthread_create) beside the current GLIBC_2.2 one, which q2-doom found breaks things. Of those
q2-classicube imports pthread_create (the renderer's second thread): it carries GLIBC_2.2, as
Q2 Pod's own programs import it. Every other symbol here stays unversioned.
If you add a libc call to the Q2 code, add it here too (the link fails until you do)."""
import pathlib, subprocess, sys

LIBS = {
    'libc.so.6': '''__libc_start_main __errno_location access bcmp bind calloc chdir clock_gettime
        close closedir exit free getenv ioctl localtime_r lseek malloc memcpy memmove memset mkdir mmap
        open opendir read readdir realloc recv rename sched_yield sigaction signal socket strerror strcmp strstr syscall
        time unlink usleep write'''.split(),
    'libpthread.so.0': [],
    'libasound.so.2': '''snd_pcm_open snd_pcm_set_params snd_pcm_writei snd_pcm_recover snd_pcm_drop
        snd_pcm_close'''.split(),
}
VERSIONED = {'libpthread.so.0': {'GLIBC_2.2': ['pthread_create']}}

out = pathlib.Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for soname, text in LIBS.items():
    versioned = VERSIONED.get(soname, {})
    c = out / (soname + '.c')
    script = out / (soname + '.map')
    script.write_text(''.join(f'{v} {{ global: {"; ".join(n)}; }};\n' for v, n in versioned.items()))
    names = text + sum(versioned.values(), [])
    c.write_text(''.join(f'void {t}(void) {{}}\n' for t in names))
    subprocess.run(['clang', '--target=mipsel-linux-gnu', '-march=mips32r2', '-mabi=32', '-mfp64',
                    '-mnan=2008', '-fPIC', '-nostdlib', '-shared', '-fuse-ld=lld', '-fno-builtin', '-w',
                    f'-Wl,-soname,{soname}', *([f'-Wl,--version-script={script}'] if versioned else []),
                    '-o', out / soname, c], check=True)
