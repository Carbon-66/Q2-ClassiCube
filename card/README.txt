# ClassiCube for the Shanling Q2 (Q2 Pod)

ClassiCube is a free, open-source block-building game in the style of Minecraft Classic:
https://www.classicube.net/

This port runs it on the Shanling Q2 as a Q2 Pod card game, offline, with its textures and sounds
included.

## Install

1. Copy the `.classicube` folder to the root of the card. On a Mac, press ⌘⇧. in Finder to see
   dot-folders.
2. Eject the card, then open **Library → Games → ClassiCube**.

The first start makes a new world. It's saved when you leave, and every five minutes.

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
| Tap | Place a block | Select |
| Hold | Break a block | |
| Hotbar | Pick a block; ⋯ opens the inventory | |

When typing, the wheel moves between keys on the on-screen keyboard, Centre presses one, and
Return closes it. You can tap the keys too.

## Features

- Both processor cores draw the world, and hidden parts are skipped, so it runs at about 30 to 50
  frames a second.
- A Minecraft-style blue sky.
- Block sounds and footsteps through headphones, Bluetooth or a USB DAC. Your music pauses while
  you play.
- Large menus, text and hotbar for the small screen.
- Settings in **Pause → Options… → Graphics options**:
  - **View distance** (64 by default): lower is faster.
  - **Render scale**: below 100% is faster but blockier. Menus stay sharp.
  - **Fast leaves** (on): solid leaves, faster among trees.
  - **Occlusion** (on): skips what's hidden.
  - **Second core** (on): off is slower but keeps the Q2 cooler.

## Files

- `maps/q2-world.cw`: your world. Delete it, or use **Pause → Generate new level**, for a fresh one.
- `texpacks/`: texture packs.
- `options.txt`: settings.
- `classicube.log`: the last run's log. Send it if something goes wrong.

## Licence

ClassiCube is © UnknownShadow200 and contributors, under the BSD 3-Clause licence
(`LICENSE-ClassiCube.txt`); this port is under the same licence.
