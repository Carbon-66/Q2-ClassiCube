# q2-classicube

[ClassiCube](https://www.classicube.net/), the open-source block-building game in the style of
Minecraft Classic, for the Shanling Q2 under [Q2 Pod](https://github.com/DiamondBond/q2-pod). It
runs as a full-screen card game in **Library → Games**, offline, with ClassiCube's own textures
and sounds.

## Install

Copy `dist/.classicube` (or the card zip's `.classicube`) to the root of the card, then open
**Library → Games → ClassiCube**.

## Controls

| Control | In the game | In menus |
| --- | --- | --- |
| Wheel | Turn | Scroll |
| Centre | Jump / swim up / fly up | Select |
| Centre twice quickly | Fly on or off | |
| Play/Pause | Fly down | Select |
| Previous / Next | Hotbar slot left / right | Previous: back |
| Return | Pause menu | Back |
| Hold Return | Save and quit | Save and quit |
| Stick (bottom left) | Walk | |
| Drag | Look around | |
| Tap / hold | Place / break a block | Select |
| Hotbar | Pick a block; ⋯ opens the inventory | |

## Features

- Both processor cores draw the world, and hidden parts are skipped: about 30 to 50 frames a second.
- Minecraft-style blue sky, block sounds and footsteps, large menus and hotbar for the small screen.
- Graphics options: View distance (64), Render scale, Fast leaves, Occlusion, Second core.
- An on-screen keyboard driven by the wheel, and saving on exit and every five minutes.

The player's guide is [card/README.txt](card/README.txt). How it was built, measured and tuned is
in [docs/NOTES.md](docs/NOTES.md).

## Building

```sh
./build.sh        # build/q2-classicube and dist/.classicube
test/run.sh       # host tests: the real main() on a fake Q2 panel, with scripted input
```

Needs `clang`, `ld.lld` and `python3` (with Pillow for the icon and tests).

## Licence

BSD 3-Clause, as ClassiCube (`classicube/license.txt`).
