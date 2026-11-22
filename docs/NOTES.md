# Development notes

How the port works and why, version by version: what changed in ClassiCube, what was measured,
and what was tried and left out.

## Changes to ClassiCube

`patches/classicube-q2.diff`, small changes, each under `CC_BUILD_Q2`:

- `Core.h`: a `PLAT_Q2` block (checked before `__linux__`): a console-style build with touch,
  the software renderer, the Q2's own audio backend (`q2/Audio_Q2.c`), no music, no networking,
  no launcher.
- `Gui.c`: under `CC_BUILD_Q2`, a widget anchored in a corner is moved in until it is clear of the
  glass's 80 px rounded corners (sideways at the top, straight up for a wide button at the
  bottom, else along the diagonal). The hotbar's default scale is 2.6, the inventory's 2.5.
- `TouchUI.c`: no on-screen jump or fly up/down buttons (Centre and Play/Pause do those), and no
  Fly or Speed buttons by default (Centre pressed twice flies; Controls → On-screen controls adds them).
- `Widgets.c`: menu buttons 46 tall, not 40 (menu rows are 50 apart).
- `Drawer2D.c`: Use system font on by default (without FreeType, ClassiCube's fallback font).
- `SystemFonts.c`: the fallback font's letter spacing a pixel tighter when scaled, so labels fit.
- `Gui.c` also: the crosshair's default scale is 1.5.
- `TouchUI.c`, `Menus.c`: the touch UI's "..." (More) isn't drawn over the game; the Game menu has
  it at the same place, top centre, and it opens More with the menu closed.
- `Graphics_SoftGPU.c`, `MapRenderer.c`, `MenuOptions.c`: occlusion culling (`q2-occlusion`, on by
  default, Graphics options in Names' place). Before a chunk's opaque parts are drawn, its 16-block
  box is projected; it's skipped if it's off the screen, or if every 8 × 8 pixel tile it covers is
  all nearer than its nearest corner. Each tile's farthest depth is worked out again only when a
  triangle that writes depth has drawn over it since. Chunks are drawn nearest first, so what's in
  front is in the depth buffer when the chunks behind are asked about. Conservative (a box crossing
  the near plane is always drawn), so frames are the same pixel for pixel: `test/occlusion.sh`
  compares ten views with it on and off. On the host, 2–48% more frames by view (`WORLD=` in
  `test/bench.sh` for another world): most where hills or walls are close.
- `TexturePack.c`: the default texture pack is read from the card only if it's there, and if it's
  missing or can't be read, ClassiCube's own textures built into the program are used
  (`q2/Platform_Q2.c` includes `classicube/misc/cc_textures.zip` with `.incbin`, 84 KB): a copy to
  the card that left `texpacks/default.zip` out had left the game without textures. `test/run.sh`
  checks both cases (`TEXPACK=none` and `TEXPACK=damaged` in `test/run-host.sh`).
- Diagnostics (`q2/Platform_Q2.c`): Q2 Pod notes nothing when a card game dies, so the log does.
  A crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) logs its signal, the address and, on the Q2,
  the pc and ra from the o32 ucontext; look them up with
  `llvm-addr2line -f -e build/q2-classicube.debug ADDRESS` (`build.sh` keeps the binary with its
  symbols there and strips the card's copy). At the start, the kernel's log (`/dev/kmsg`, since the
  Q2 started) is searched for lines about the game or memory running out, which is how a run the
  out-of-memory killer ended shows up. The memory in use (`VmRSS`, `VmHWM`, `MemAvailable`) is logged at
  the start, with the world loaded and every 30 s, and the last run's log is kept as
  `classicube-previous.log`.
- Screen pacing (`q2/Window_Q2.c`): at most one page swap (`FBIOPAN_DISPLAY`) a refresh of the
  panel. The game swapped as fast as it drew; demo pans and then waits for the refresh
  (`FBIO_WAITFORVSYNC`, `0x80044620` in MIPS's ioctl numbering), and q2video and Doom run under the
  refresh rate. From 1.4 (occlusion culling, faster frames) the whole Q2 froze on the game's last
  frame after 2 to 3 minutes, the log ending with no crash, with memory to spare: a swap stuck in
  the screen's driver is the guess. The refresh is timed at the start (six waits, logged); a swap
  less than a refresh after the last waits for the next refresh. Below about 57 frames a second
  nothing waits. `q2-pace=false` turns it off (`test/bench.sh` does, to measure).
- Stall watcher (`q2/Platform_Q2.c`): a thread that, when no frame has been drawn for 5 s (and at
  20 s and 60 s), logs what the main thread was doing (`Q2_Phase`, set around the swap, the wait
  for the second core, a save) and each thread's state and kernel function (`/proc/self/task/N/stat`
  and `wchan`). `FB_HANG_AT=N` makes `test/fbshim.c`'s Nth swap never return, to see it work.
- Health and heartbeat (`q2/Platform_Q2.c`): the Q2 froze whole after 1.5 to 4 minutes with no
  stall report in the log, so the kernel itself, or the card, stopped. With both cores busy and no
  cpufreq in the kernel (no throttling), heat or a sagging battery are suspects: every 30 s the log
  gets the thermal zones' temperatures, the battery's voltage, charge and current
  (`/sys/class/power_supply/battery`) and `/proc/loadavg`. Every 10 s the watcher thread writes the
  last 12 such lines to the Q2's own flash (`/mnt/data/q2-classicube.beat`, written at once), which
  a clean end removes; at the next start a file still there goes into the log as the last run's
  final notes, so they survive a card that stopped answering.
- `MenuOptions.c`, `Graphics_SoftGPU.c`: Second core (`q2-cores`) in Graphics options, in Mipmaps'
  place (the software renderer has none), switched between draws.
- Defaults, 1.8 (`q2-defaults2`, applied once, also over an older `options.txt`): view distance 128,
  Show FPS off; 1.9 (`q2-defaults3`): view distance 80.
- The battery in the health notes is the CW2015 fuel gauge's charge
  (`/sys/devices/i2c-0/0-0062/cw2015_capacity`, as Q2 Pod's icon reads it; the AXP2101's own
  `capacity` stays near 100), with the AXP's voltage, current, temperature and status where it has
  them; CPU temperature from a thermal zone or hwmon, where the kernel has one (on the Q2, none).
  Freezes came with both cores drawing and the gauge at 18 to 20%; none with Second core off.
- Two cores, reworked (1.10): the whole Q2 froze after minutes with both cores drawing, on the
  charger too, and never with one; Q2 Pod's author pointed at the timings between the cores. The
  worker reads no clock now (it read it every 1024 looks while it waited); a wait, on either side,
  looks at the other's counter for about 1 ms (a count the main thread times once at the start)
  and then sleeps in the kernel on a futex (`syscall`, o32 number 4238) until the other wakes it,
  with sequentially consistent counters and asleep flags so no wake-up is missed; and each core
  fills its own 16-row bands, not alternate rows, so their writes seldom share a cache line. As
  fast as before on the host (`test/bench.sh`), and at a paced 60 frames a second the worker uses
  about a fifth of a core where it used all of one. Frames differ from one core's in a few pixels
  at rounding ties, spread through the bands, not along their edges.
- Speed note (1.11): every 30 s the log gives the frames a second and the view distance, and with
  two cores the hand-offs a frame, each side's sleeps and the share of the time the main thread
  waited for the worker. `q2-bandrows` (1, 2, 4, 8, 16; 16 by default) sets the bands' height:
  on the host at view distance 80, 16 drew fastest and 1 slowest, by about a tenth. From 1.6 to
  1.10 the frame rate fell from about 25 to 20 on the Q2: the view distance, 32 then 80 (80 cost
  about a fifth on the host too), not the reworked hand-off (the same speed on the host).
- Default view distance 45 (1.12, `q2-defaults4`). 1.12's fog (linear from 60% of the view
  distance, in the software renderer and the minimal environment mode) was taken out in 1.13: it
  hid the world's edge but not the water fault it was meant for.
- See-through water (1.14, made cheaper in 1.15): the sea showed dark squares and lines, the
  bedrock under the map, coming and going as the camera moved.
  - `MapRenderer.c`, `Vectors.c`: chunks past the far plane (at the view distance) weren't drawn,
    tested one by one, so from above a sea-level chunk could be drawn while the one under it
    wasn't, and the eye went through the water to the bedrock under the map. Now a chunk is in
    range by its distance across the map only (`Q2_FlatDist`: a column is in range whole or not
    at all), and in view by a box (`Frustum_TestBox`), stretched up to the sea's level for a
    chunk under it: the floor under the water is drawn whenever the water is in view. The sort,
    nearest first, still counts the height, so occlusion culling still finds what's in front.
    (1.14 sorted across the map and stretched every box to the sky: about a fifth slower.)
  - `EnvRenderer.c`: a column whose sea-level chunk isn't drawn (`MapRenderer_Q2ColumnShown`)
    gets the border's water at sea level, its texture and tint, where it's on the screen (past
    the far plane too): past what's drawn the map looks like the ocean around it, and a far
    hilltop drawn without the chunks under it stands on that sea. OpenGL builds hide all this
    with fog (1.12's fog, taken out, hid the edge but not the rest).
  - `Graphics_SoftGPU.c`: a texel's row is held within the triangle's corners' rows (integers,
    before the wrap): at a water face's edge, seen low, it ran into the next tile of the atlas,
    empty texels, and that row of water was drawn clear (a line of the dirt under it showed).
    Rows only: the atlas is one tile wide. And the map's border is built of 64-block quads
    (`GFX_LIMIT_MAX_VERTEX_SIZE`, Q2's size in `EnvRenderer_AxisSize`), not 2048: a huge quad
    near the eye projects past the float edge functions' precision, and whole triangles of the
    border water dropped out.
  `test/compare.sh` puts host builds side by side: 1.15 drew 0 to 30% more frames than 1.13 in
  most views and 25 to 40% more than 1.14 (from 6% fewer to 32% more against 1.13 by view).
- Touch (1.16): the thumbstick is a tenth bigger (`Gui.c`: touch scale 1.1 by default) and 14 px
  higher (`TouchUI.c`), clear of the hotbar's left end. And it stuck walking now and then until the
  next tap: Q2 Pod sends input with `MSG_DONTWAIT`, an AF_UNIX datagram socket holds 10 unread
  (`net.unix.max_dgram_qlen`), and the game read them only between frames, so a slow frame let
  moves and wheel ticks fill the queue and the release after them was dropped. A thread now
  reads the socket as datagrams come (`q2/Window_Q2.c`, a blocking `recv` into a 512-event ring
  the main thread empties between frames); a full ring drops moves and wheel ticks, never a
  press or a release. And a press while one is still down (a release lost before it reached the
  game: Q2 Pod's own queue can drop one) releases the old first. `test/run.sh` checks it:
  `test/stuck.txt` sends a burst and its release right behind, as Q2 Pod sends (`Q2POD_DROP=1` in
  `test/send.py`), at 30 frames a second; 1.15 stuck in 3 of 5 tries, 1.16 in none.
- Render scale (1.17, `q2-renderscale`, 100 by default; Graphics options in Smooth lighting's
  place, 100/90/80/75/67/50%): `Graphics_SoftGPU.c` draws the world in a smaller picture and
  stretches it over the view's (the nearest pixel) at the first 2D draw, so the hotbar, text and
  menus are drawn at full size on top. Counted by `test/count.sh` (callgrind, one core, the bench
  view), instructions a frame against 1.16: 100% 3% fewer, 75% 33% fewer, 67% 38%, 50% 53%; on
  the host, frames a second +22%, +33% and +55% at 75, 67 and 50%. Below 100% occlusion culling's
  tiles are coarser against the world and skip a little less (8% more triangles at 67%).
  Also in 1.17: a row of a triangle runs from where its edges cross the row, to a thousandth of a
  pixel, not a pixel either side (each still tested), with the crossings worked out by multiplying
  by 1/d per triangle, not dividing per row: the 3% at 100%. And the frame's turn onto the panel
  (`Q2_Blit`) writes each panel row in address order.
  Tried for 1.17 and left out, by the counts: drawing the frames the panel's way round, so they
  needn't be turned (15% more instructions a frame: block faces seen at a slant, wide and short in
  the view, are tall and narrow on the panel, many more rows of a few pixels each, and each row
  costs its setup; and the turn was cheap already, writing the screen in order and reading down
  the frame's columns, 320 cache lines that a 32 KB cache holds); and a tile's average colour in
  place of its texture past 24 blocks (no measurable gain: very few pixels are that far at view
  distance 45, even over open water, and long merged faces run from near to far).
- Touch (1.18): the stick still stuck now and then, mostly walking and then jumping. Q2 Pod's
  internals.md (Input queue) has the cause upstream of the game: the screen's and the wheel's
  threads share state in the firmware's touch library (`pthres.so`), and a thumb on the ring (Centre
  sits inside it) can drop the screen's release before Q2 Pod sees it, so nothing reaches the game.
  But Q2 Pod posts a move for every sample while a finger is down, a resting finger's too (the
  same place again). So `q2/Window_Q2.c` (`TouchQuietCheck`) lets a held touch go, where it last
  was, once nothing has come from the screen for `q2-touchquiet` ms (400 by default; 0: never),
  timed by the input thread as datagrams arrive. It's armed only once three such repeats have been
  seen in a run (the log says so), so where a resting finger sends nothing a held stick is never
  dropped. The log notes touches held over a second (the first 12): their moves and the longest
  wait between them, to tune the 400. `test/quiet.txt` (in `test/run.sh`): a resting stick's moves
  20 ms apart, then none and no release; `hold N X Y MS` in `test/send.py` sends them.
- `-O3` (1.18, `OPT=-O3 ./build.sh`, into `build-O3/` and `dist-O3/`; the log's first line says
  which): the triangle filler, most of a frame, compiles to the same MIPS instructions at `-O2` and
  `-O3` (1076, reordered a little); elsewhere `-O3` inlines more (the binary is 697 KB against
  668 KB). Counted on the host (`test/count.sh`, both at each level), `-O3` ran 0.4% fewer
  instructions a frame. clang emits no `recip.s` for `1/w` with `-freciprocal-math` either. The
  card build stayed `-O2` in 1.18; from 1.19 it's `-O3` (it ran well on the Q2), and
  `OPT=-O2 ./build.sh` builds into `build-O2/` and `dist-O2/`.
- Touch (1.19): 1.18 let go of a held stick on silence alone, and on the Q2 a thumb resting still
  sends nothing for longer than 400 ms, so walking stopped. The release can only be lost after the
  ring is used: the firmware's pressure filter keeps one state for the screen and the ring, the
  ring's lift marks the screen's finger lifted, and the screen's next sample sets it right. So now
  a touch is let go for silence (`q2-touchquiet`, 700 ms by default) only after wheel travel or a
  Centre press (stamped by the input thread as they come, plus 300 ms for the thumb to leave the
  ring) with nothing from the screen since. And a stick or button let go that way is pressed again
  at the next move from the screen, the thumb still there; not a touch on the world (pressed again
  and lifted, it would be a tap, a block placed). The log notes each let-go and each press again,
  with times. `test/quiet.txt` (in `test/run.sh`): silence alone keeps the stick held; Centre then
  silence lets it go; a move presses it again.
- Touch (1.20): 1.19's log from the Q2 showed a thumb resting on the stick sends nothing for many
  seconds (3.6, 9.3, 15.4 s), so every let-go there was a thumb still on the stick (each pressed
  again 0.5 to 2.7 s later), and no silence can tell a resting thumb from a lost lift. The kernel
  can: the touchscreen's input device (`hyn_ts` in `/proc/bus/input/devices`, as Q2 Pod's
  `tools/q2remote.py` names it) reports every lift as `BTN_TOUCH 0`; the release is lost later,
  in tslib's pressure filter in Q2 Pod's process. A thread in `q2/Window_Q2.c` reads that device
  alongside Q2 Pod (no grab), and a touch the game still holds 300 ms after the screen's lift is
  let go (the log says so). The silence rules, the re-press and `q2-touchquiet` are gone; until
  the device has sent an event nothing is let go, so where it can't be read touches behave as in
  1.17 (a lost lift held until a tap). `Q2_TOUCHDEV` names another file to read: `test/lift.txt`
  (in `test/run.sh`) writes BTN_TOUCH events to a FIFO (`ev 1`, `ev 0` in `test/send.py`): a
  resting thumb and Centre keep the stick, the device's lift with no release lets it go, a tap
  with its release lets nothing go. On the Q2 the frame rate in that log was 35 to 52 a second.
- 16-bit depth buffer (1.21, `Graphics_SoftGPU.c`; `-DQ2_DEPTH16=0` builds the float one): z/w is
  stepped across a row as an integer in 65280ths shifted up 15, so the value to test and store is
  one shift away, and a pixel's test is a shift, a halfword load and an unsigned compare (below the
  near plane the shift lands past any stored depth) where it was two float compares. 65280ths, not
  65536ths: past the far plane z/w reaches about 1.002 (the stand-in sea is drawn there), which then
  still fits, in order. The buffer is 240 KB, not 480, to read, write and clear. The occlusion
  tiles read it as (d + 1) / 65280, an upper bound, and 0xFFFF (cleared) as farther than anything.
  At the view distance (45) the step between two depths is about a third of a block; frames
  differ from the float buffer's in a few hundred single pixels along block edges (ties), in nine
  views, and `test/occlusion.sh` passes. Counted (`test/count.sh`, bench view, one core): 2% more
  instructions on the host build (x86), 18% fewer L1 and 15% fewer last-level cache misses; on
  MIPS the test compiles to fewer instructions than the float one (5 with its delay slot, against
  7). Frames a second on the host, two cores: 2 to 8% more.
  Fewer divisions per row was done in 1.17 (1/d per triangle). The divide left is per pixel
  (1/w, for the texture); `q2-texspan=8` does it every 8 pixels and steps between, which drew no
  faster on the Q2 when tried (1.5), before the rest of the frame got cheaper.
- Default view distance 64 (1.22, `q2-defaults5`, applied once over an older `options.txt`), with
  1.17 to 1.21's speed. On the host, two cores, 64 against 45: 18% fewer frames in the bench
  view, 4% looking east from the spawn, 8% over the sea. The tests and benchmarks set their own
  (45) and mark `q2-defaults5` so it isn't applied over them.
- Sky (1.23): the minimal environment draws no sky mesh, so the sky was the clear colour, the fog's
  near-white. Now `Graphics_SoftGPU.c` (`Q2_SkyFill`) fills each row of the picture with a colour
  for how high the middle of that row looks (from this frame's view and projection, so it's done
  at the first 3D draw, not at the clear, which comes before the camera's update): Minecraft's
  daytime fog colour (#C0D8FF) at and below the horizon, easing to its plains sky (#78A7FF) by
  about 25 degrees up. A world with its own sky and fog colours gets those instead
  (`EnvRenderer.c`); underwater or in lava (a block with its own fog) the clear colour as before.
  It costs what the clear did: a colour written per pixel.
- Occlusion, finer (1.19). Counted with a frame's chunk numbers kept per pixel (a temporary
  build): in the bench view 11 of the 20 chunks drawn had no pixel left in the frame, half the
  triangles drawn; 40 to 60% in most views. Not hidden behind what's drawn later (the chunks are
  drawn nearest first, so last frame's depth would have added little): the chunks under and
  beside the eye, whose boxes reach behind it and can't be judged whole, and faces behind hills in
  chunks partly in view. So:
  - `Graphics_SoftGPU.c` `Q2_QuadHidden`: each quad, once its corners are on the screen, is tested
    against the occlusion tiles as chunks are, and skipped when all behind (at most 12 tiles,
    `q2-quadtiles`; a larger quad is seldom hidden and its test cost more than it saved in a
    cave). With two threads a quad in none of a thread's bands is skipped by it at once, and each
    thread marks and tests only its own bands' tiles. Both threads read the tiles while drawing:
    the depth buffer only gets nearer within a frame, so a far depth read early hides less, never
    wrongly. In the bench view triangles set up fell from 7255 a frame to 1317.
  - `Builder.c` `Q2_ChunkBounds`, `MapRenderer.c`: a chunk is tested by the box around its
    vertices, not the whole 16-block cube, whose top reached into the sky's tiles.
  Frames are the same as 1.18's to the pixel (`test/occlusion.sh`, and 1.18 against 1.19 in eight
  views). Instructions a frame (`test/count.sh`, one core): bench 10.6% fewer, cave 2.6% fewer.
  Frames a second on the host, two cores: bench +13%, the spawn looking east +19 to 26%, looking
  down +18%, over the sea +7 to 20%, the cave about the same.
- `Camera.c`, `MenuOptions.c`: mouse sensitivity (the wheel's turning speed) 42 by default, not 30.
- `Block.c`, `TexturePack.c`, `MenuOptions.c`: Fast leaves (`q2-fastleaves`, on by default, in
  Graphics options in 3D anaglyph's place): leaves drawn as opaque blocks, so the faces between
  two leaves are culled, and the leaves tile's see-through pixels filled with 45% of its average
  colour (else the sky shows through a canopy whose inner faces are gone). Switched in the menu,
  the leaves block is reset, the textures reloaded and the world rebuilt. About 13% more frames on
  the host looking into trees (`BENCH_OPTS=q2-fastleaves=true INPUTS=... test/bench.sh`).
- `Graphics_SoftGPU.c`: triangles filled from their corners' exact positions, not cut down to
  whole pixels, with a hundredth of a pixel's tolerance at the edges: cut down, faces meeting at
  a T-junction left hairlines the sky showed through (about 5% slower).
- `Graphics_SoftGPU.c`: a faster triangle fill (the CPU's work for most of a frame): each row runs
  only across the triangle, depth is z/w stepped per pixel and tested before the one divide (for
  the texture), and every value is stepped by an add. About 26% more frames on the host benchmark
  (`test/bench.sh`, a fixed world and view); the picture differs only in a few pixels at ties.
  And two threads (16-row bands each since 1.10, see above): each batch of 3D quads is drawn by the main thread (even rows, then) and a worker
  (odd rows), the main thread waiting for it before it returns, so state never changes mid-draw.
  About 30% more frames than one thread on the host (`CORES=1 test/bench.sh` against 2); the
  corners are transformed by both, and the interface stays on the main thread. `q2-cores=1` in
  `options.txt` turns it off. Needs `libpthread.so.0` (`pthread_create@GLIBC_2.2`).
  And `q2-texspan` (0 by default, at most 16): texture coordinates exact (a divide) only every
  that many pixels along a row, stepped linearly between, as Quake did. No faster on the x86
  host, nor on the Q2 at 8 (measured on the device), so off: the divides aren't what limits it. Splitting the corner transforms between the two threads was tried and
  measured slower (the extra wait and the shared list cost what it saved): not kept.

To move to a newer ClassiCube: replace `classicube/` with the new source, apply the diff, update
`CLASSICUBE_COMMIT`, and run `./build.sh` and `test/run.sh`.

## What it doesn't do yet

- **Music.** The sounds play (`q2/Audio_Q2.c`: a mixer thread on ALSA, Q2 Pod's `audio=1`
  contract), but music is off: ClassiCube's music is Ogg Vorbis, streamed, which the backend
  doesn't take, and isn't shipped.
- **Speed on the device is unmeasured** (`test/bench.sh` measures the host build). The host runs it at well over 100 fps; the X2000 is far
  slower, and the view distance starts at 32 for that reason.
