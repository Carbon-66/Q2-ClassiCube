#!/usr/bin/env python3
"""send.py SCRIPT SECONDS: plays SCRIPT's lines ("AT DATAGRAM", AT in seconds from now; a DATAGRAM
of "snap NAME" copies the frame shown to test/out/NAME.png instead) to Q2 Pod's input socket,
then waits until SECONDS have passed. A DATAGRAM of "burst N X Y [MS]" sends N touch moves
around (X, Y), MS milliseconds apart (0: at once); "hold N X Y MS" sends N moves to (X, Y) itself, a
resting finger as Q2 Pod reports one; "ev 1" or "ev 0" writes BTN_TOUCH down or up to the FIFO named by
Q2_TOUCHDEV (the game reads it as the touchscreen's device). With Q2POD_DROP=1 a datagram goes as Q2 Pod sends it (MSG_DONTWAIT), dropped when
the game's queue is full, not waited for."""
import os, shutil, socket, struct, subprocess, sys, time
start = time.time()
sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
lines = [l.split(None, 1) for l in open(sys.argv[1]) if l.strip() and not l.startswith('#')]
for at, msg in lines:
    while time.time() - start < float(at): time.sleep(0.01)
    msg = msg.strip()
    if msg.startswith('snap '):
        name = msg.split()[1]
        shutil.copy('test/out/frame.raw', f'test/out/{name}.raw')
        subprocess.run([sys.executable, 'test/view.py', f'test/out/{name}.raw', f'test/out/{name}.png'])
        continue
    if msg.startswith('ev '):  # the touchscreen device: a finger on (1) or off (0), BTN_TOUCH
        global tsdev
        if 'tsdev' not in globals(): tsdev = open(os.environ['Q2_TOUCHDEV'], 'wb', buffering=0)
        tsdev.write(struct.pack('<qqHHi', 0, 0, 1, 0x14a, int(msg.split()[1])) + struct.pack('<qqHHi', 0, 0, 0, 0, 0))
        continue
    msgs = [msg]
    if msg.startswith('burst ') or msg.startswith('hold '):
        a = [int(v) for v in msg.split()[1:]]
        n, x, y, gap = a[0], a[1], a[2], (a[3] if len(a) > 3 else 0)
        if msg.startswith('hold '):  # a resting finger: the same place again and again
            msgs = [f't m {x} {y}' for i in range(n)]
        else:
            msgs = [f't m {x + (i % 5) - 2} {y + (i % 3) - 1}' for i in range(n)]
    for i, m in enumerate(msgs):
        if i and gap and msg.split()[0] in ('burst', 'hold'): time.sleep(gap / 1000)
        try:
            if os.environ.get('Q2POD_DROP'): sock.sendto(m.encode(), socket.MSG_DONTWAIT, '/tmp/q2-classicube.input.sock')
            else: sock.sendto(m.encode(), '/tmp/q2-classicube.input.sock')
        except FileNotFoundError: pass  # the game has gone (a tap's release after Quit)
        except BlockingIOError: pass    # the game's queue full: dropped, as Q2 Pod's are
while time.time() - start < float(sys.argv[2]): time.sleep(0.05)
