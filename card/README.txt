# ClassiCube for the Shanling Q2 (Q2 Pod)

ClassiCube is an open-source block-building game in the style of Minecraft Classic. This port
runs it as a Q2 Pod **card game**: it sits in **Library → Games** under Sudoku, Vortex, Solitaire
and Snake, and runs full screen. Rockbox is never involved. Everything it needs is in this folder,
textures included, so it never goes online.

## Install

1. Copy the `.classicube` folder to the **root of the card** (next to your music folders). On a
   Mac, press ⌘⇧. in Finder to see dot-folders.
2. Eject the card, then open **Library → Games → ClassiCube**.

The first start makes a new world. It's saved when you leave and every five minutes, and the
next start carries on where you were.

## Controls

The Q2's touchscreen takes one finger at a time, so the wheel does the turning.

| Control | In the game | In a menu or the inventory |
| --- | --- | --- |
| **Wheel** | Turn (anticlockwise turns left; faster with **Controls… → Mouse sensitivity**, 42 by default) | Scroll |
| **Centre** | Jump (swim up in water); in flight, go up | Enter |
| **Centre twice quickly** | Fly on or off, as a double jump in Minecraft | |
| **Play/Pause** | In flight, go down | Enter |
| **Previous / Next** | The hotbar slot to the left / right | Previous: back |
| **Return** | Pause menu | Back |
| **Hold Return** | Save and leave (Q2 Pod's own) | Save and leave |
| **Touch: the stick, bottom left** | Hold and drag up to walk, down to back off, sideways to strafe (**Gui options… → Touch scale** sizes it: 1.1 by default) | |
| **Touch: drag anywhere else** | Look around (up and down too) | |
| **Touch: tap** | Place a block | Select |
| **Touch: hold** | Break a block | |
| **Touch: the hotbar** | Pick a block; tap ⋯ at its end for the inventory | |
| **Touch: ⋯ at the top of the Game menu** (Return) | More: Fog (cycles the view distance), Controls (tap and hold actions, on-screen buttons such as Fly and Speed), Main menu, Chat, Screenshot | |

**Typing** (a level's name, chat, a number in Options) brings up an on-screen keyboard or numpad.
The **wheel** moves the highlight from key to key, in reading order (clockwise forward), and
**Centre** presses the highlighted key. Its **Enter** finishes: it presses OK, or sends the chat
message. **Return** closes the keyboard. Tapping keys works too.

If the stick ever keeps walking after you lift your thumb (the Q2 can lose the lift when the
wheel or Centre was touched), the game reads the touchscreen itself and lets it go within 0.3 s.

Centre pressed twice quickly turns flying on and off. In flight, hold Centre to go up
and Play/Pause to go down (each press goes down a little, holding keeps going).

If the game says the Play/Pause and side buttons control your music, the folder's `q2app` is an
old one: copy the new `q2app` in (it has `audio=1`).

## Sound

Blocks make their sounds as you break and place them, and footsteps follow what you walk on:
ClassiCube's own sounds, through the headphone jack, Bluetooth or a USB DAC, whichever Q2 Pod is
using. They follow Mono Audio and Swap L/R. **Options… → Misc options… → Sounds volume** sets
their level (0 is off). ClassiCube's sounds are recorded quietly, so they're played 4× louder
(`q2-soundboost=400` in `options.txt`, a percentage). There's no music.

**Your music stops while ClassiCube runs**, as it does for a video, and stays stopped after: Q2
Pod hands a game the sound output and the Play/Pause and side buttons together.

**Quit game** in the pause menu, or holding Return, saves the world and goes back to Games. Five
minutes without input does the same, so a forgotten game doesn't keep the screen lit.

## Size on the small screen

Menus, buttons and text are drawn larger than ClassiCube's other small-screen ports, as large as
its two-column menus allow (they then span the screen's width). The hotbar is 2.6× and the
inventory 2.5×, menu buttons are thicker, and the on-screen keyboard's keys are enlarged. **Use system
font** (Gui options) is on: ClassiCube's own larger, clearer lettering. **Options… → Gui options…** has Hotbar scale
and Inventory scale. The overall size is `q2-uiscale` in `options.txt` (0.58; 0.5 is smaller,
and above about 0.6 the menus run off the screen).

## Speed

The Q2 has no graphics chip: ClassiCube draws every frame on the CPU. The view distance starts at
64 blocks (occlusion culling skips what's hidden, so a long view costs less than it would). **Pause menu → Options… → Graphics options… → View distance**, or Pause menu → ⋯ → Fog,
changes it: shorter is faster. The FPS line is off; **Gui options… → Show FPS** brings it back.

Both of the Q2's processor cores draw the world. **Graphics options… → Second core** off draws with
one (`q2-cores=1` in `options.txt`): slower, but the Q2 works less hard and stays cooler. `q2-texspan=8` (or 16) positions textures
exactly only every 8 pixels along a row and estimates in between; on the Q2 it drew no faster, so
it's off (0) unless you set it.

**Graphics options… → Fast leaves** (in 3D anaglyph's place) draws leaves as solid blocks, as Minecraft's Fast graphics:
the faces inside a tree aren't drawn, so it's faster where there are trees (about 13% more frames
looking into a wood on the host test), at the look of dark-speckled, solid canopies. On by
default (`q2-fastleaves` in `options.txt`); turn it off for see-through leaves. It changes at once.

**Graphics options… → Occlusion** skips the parts of the world hidden behind hills, walls and the
ground in front, without drawing them, and the picture stays exactly the same. On by default
(`q2-occlusion`); it gains most among hills and close to walls (about 30–50% more frames on the
host test there) and little looking out over open water. It takes the place of Names (other
players' names: there are none offline).

**Graphics options… → Render scale** draws the world smaller and stretches it to the screen: 100%
(the default) is full detail; 75% is about a third less work for the processor, 67% a little less
again and 50% about half, at the look of bigger, blockier pixels. The hotbar, text and menus stay
sharp at any setting. It takes the place of Smooth lighting (off on the Q2: `gfx-smoothlighting` in
`options.txt` still works). It changes at the next frame.

## Files

Everything ClassiCube keeps is in this folder:

| File | What it is |
| --- | --- |
| `maps/q2-world.cw` | Your world. Delete it for a fresh one at the next start, or use **Pause → Generate new level**. |
| `maps/` | Also levels you save with **Save level** (ClassiCube's own `.cw` files work on a computer too). |
| `texpacks/default.zip` | ClassiCube's own textures. Another texture pack in `texpacks/` can be picked in **Change texture pack**. The game has a copy of these built in, used if this file is missing or damaged. |
| `options.txt` | Settings, written as you change them. |
| `classicube.log` | What the last run printed, each button press included (with its time in ms). Check it if something goes wrong. It also notes the memory in use every 30 seconds, a crash (the signal and where), and what the Q2's kernel said about the game or memory running out. |
| `classicube-previous.log` | The run before's log, kept at each start, so a crash's log survives starting the game again. |

## How it fits Q2 Pod

`q2app` says `game=1` (a full-screen card game in Games) and `audio=1` (the sound output and the
media buttons). Q2 Pod forks `.classicube/q2-classicube`
and stops painting while it runs:

- **Screen.** ClassiCube's software renderer draws 375 × 320 frames, which are turned onto the
  320 × 375 portrait panel (`/dev/fb0`), drawn on the hidden page and panned, as Q2 Pod's video
  player does. The touch buttons are kept 24 px in from the edges, clear of the glass's corners.
- **Input.** Q2 Pod sends text datagrams to `/tmp/q2-classicube.input.sock`: `k d|u CODE` for
  keys, `t d|m|u X Y` for touch and `w N` for the wheel's travel.
- **Sound.** `audio=1`: Q2 Pod stops the music and hands over the sound output, with
  Play/Pause, Previous and Next. The output comes as the program's arguments (the ALSA device,
  and off the jack a volume) and is opened as Q2 Pod's video player opens it. The sounds are
  `audio/default.zip`, ClassiCube's own.

## Licence

ClassiCube is © UnknownShadow200 and its contributors, under the BSD 3-Clause licence
(`LICENSE-ClassiCube.txt`). The textures and sounds are ClassiCube's own, from its repository
(`misc/cc_textures.zip`, `misc/cc_audio.zip`). The Q2 port is under the same BSD 3-Clause licence.
