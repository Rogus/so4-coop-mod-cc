# so4coop: local co-op for STAR OCEAN - THE LAST HOPE

so4coop adds battle co-op for up to 4 players to the Steam version of *STAR OCEAN - THE LAST HOPE -
4K & Full HD Remaster* (app 609150). Each controller drives one party member, and everyone moves and
fights at the same time.

The mod is a single `xinput1_3.dll` proxy written in freestanding C (no CRT), plus a settings file.
It contains no game files.

**Current release: [1.5](https://github.com/Rogus/so4-coop-mod-cc/releases/tag/v1.5)**

| Tested with | |
| --- | --- |
| Game | Steam build 2501532 |
| `StarOceanTheLastHope.exe` SHA-256 | `284b237259c73c4b02545a2e985eba739b3a68ea365384ebed6666ea2a01a4aa` |
| System | Linux with Proton 10 |
| Windows | Not tested |
| Other mods | [SO4Fix](https://github.com/Lyall/SO4Fix) v0.9.0 by Lyall ([setup](#using-so4coop-with-so4fix)) |

## Features
- **Battle co-op.** Every active player moves, attacks, and uses their own skills, including L2/R2.
  Characters whose players are idle go back to the AI.
- **Join mode** (`coop_join=1`, new in 1.5). In battle, each controller takes the first free
  character at its first button press. R3 moves it to the next free one. Up to 8 controllers can be
  connected; a battle has 4 characters.
- **Fixed controller mapping** (the default). `steamN_position` assigns each Steam controller to a
  party position.
- **Keyboard as a controller** (Linux, new in 1.5). `tools/kbpad.py` turns a keyboard into a virtual
  Xbox 360 controller.
- **Steady display.** The mini-map, sight cursor, and target board stay on one character instead of
  flickering between players.
- **Press guard.** One button press can't reach two characters.
- **Tag-team mode** (`coop_auto=0`). R3 makes that controller's character the leader.
- **PlayStation button prompts** (optional). The mod loads Special K texture packs by itself, without
  Special K. No pack is included.

## Testing status

| Feature | Status |
| --- | --- |
| Co-op with 2 and 3 controllers | Works in game |
| Co-op with 4 controllers at once | Supported, not yet tested |
| Join mode, local controllers | Works in game with 3 controllers |
| R3 character switch in join mode | One switch recorded in game |
| Join mode with remote guests | Not yet tested |
| `xbox_position` | 1.4.2's first version picked the wrong controller; the fix passes its self-test but hasn't been confirmed in game |
| PlayStation button prompts | Works in game |
| SO4Fix alongside so4coop | Works in game on Linux with Proton 10 |
| Windows | Not tested |

Testing so far is manual playtesting (rounds 1-48) plus self-tests for parts of the mod, such as
controller mapping, join mode, and the press guard. There is no automated in-game test.

## Known issues
- Co-op works in battle only. In the field and in menus, one player leads as usual.
- During co-op, the mini-map, sight cursor, and target board follow one character
  (`coop_display_pos`), not whoever has the current turn.
- The mod starts acting about 45 seconds after the game launches.
- There is no built-in online play. Join mode is meant for Steam Remote Play Together or Sunshine.
  Guests need a controller, because a guest's keyboard moves whoever has the current turn.

## Install
1. Download `so4coop-coop-1.5.zip` from the
   [releases page](https://github.com/Rogus/so4-coop-mod-cc/releases) (or [build it](#build)
   yourself) and extract `xinput1_3.dll` and `so4coop.ini` into the game folder, next to
   `StarOceanTheLastHope.exe`.
2. On Linux or Steam Deck, set the game's launch option in Steam (skip this on Windows):

   ```sh
   WINEDLLOVERRIDES="xinput1_3=n,b" %command%
   ```

3. Keep Steam Input **enabled** for the game. With it disabled, the game gets no controller input.
4. Optional, for PlayStation button prompts: copy the texture pack's `SK_Res` folder into the game
   folder, so that you have `SK_Res\inject\textures\61a1af48.dds`. Don't install Special K itself.

To uninstall, delete `xinput1_3.dll`, `so4coop.ini`, and `so4coop.log` from the game folder, along
with `SK_Res` if you added it, and remove the launch option.

## Using so4coop with SO4Fix
[SO4Fix](https://github.com/Lyall/SO4Fix) by Lyall is a separate, optional mod. It adds custom
resolutions, borderless fullscreen, ultrawide support, and correct FOV, and it fixes HUD and shadow
issues. so4coop doesn't include it or require it.

1. Download SO4Fix from its [releases page](https://github.com/Lyall/SO4Fix/releases) and extract it
   into the game folder.

On Linux, SO4Fix's own instructions say to override `winmm`. In testing with this game on Proton 10,
Proton loaded its built-in `winmm` instead, and SO4Fix never ran. These extra steps fixed that:

2. Rename SO4Fix's `winmm.dll` to `wininet.dll`.
3. Put `msvcp140.dll`, `vcruntime140.dll`, and `vcruntime140_1.dll` version 14.40 or newer in the
   game folder, or install Visual C++ 2015-2022 in the Proton prefix. The game's prefix usually has
   14.0, and with that version the game closes right after launch.
4. Use this launch option instead of the one above:

   ```sh
   WINEDLLOVERRIDES="xinput1_3=n,b;wininet=n,b;msvcp140=n,b;vcruntime140=n,b;vcruntime140_1=n,b" %command%
   ```

If `SO4Fix.log` appears in the game folder, SO4Fix loaded. Please report problems with SO4Fix itself
to the [SO4Fix project](https://github.com/Lyall/SO4Fix/issues), after checking that they also happen
without so4coop.

## Choosing who plays whom
Settings are in `so4coop.ini`. Party positions count from 0 (the first member).

**Fixed mapping:** `steamN_position=P` gives Steam controller N party position P. Steam numbers
controllers in its own order, which can change between sessions. To find a controller's number, press
R3 on it in battle and look in `so4coop.log` for a line like
`steamctl ... controller 2 action 10: pressed`.

**Join mode:** set `coop_join=1`. Controller order no longer matters, and `steamN_position` and
`xbox_position` are ignored. This is the mode to use for remote guests.

| Setting | Default | Meaning |
| --- | --- | --- |
| `steam1_position` to `steam8_position` | 0, 1, 2, 3 for the first four | Party position of each Steam controller |
| `xbox_position` | not set | Party position of the Xbox controller; `steamN_position` then counts only the others |
| `coop_join` | 0 | 1 = join with a button press |
| `coop_auto` | 1 | 0 = tag-team mode instead of co-op |
| `coop_slice_ms` | 40 | Length of each player's turn |
| `coop_idle_ms` | 1500 | Time without input before a player stops getting turns |
| `coop_active_only` | 1 | 0 = mapped players get turns even when idle |
| `coop_press_guard_ms` | 50 | No turn change within this time of a button press; 0 = off |
| `coop_display_fix` | 1 | 0 = display follows the current turn and flickers |
| `coop_display_pos` | lowest mapped position | Character the display follows |
| `tex_hook` | 1 | 0 = never load texture packs |

Leave `hook_steam=1` and `leader_only_input=1` as they are; co-op needs both. The full reference is
in [release/README.txt](release/README.txt), also available in
[Portuguese](release/LEIA-ME.txt).

## Keyboard as a controller (Linux)
You need Python 3 with `python-evdev`, read access to the keyboard, and write access to `/dev/uinput`.
`tools/70-so4kbpad.rules` grants the read access, but it's written for one specific keyboard
(`248a:16ab`) and for keyd. Change the IDs to match yours.

Use the launcher as the game's launch option. It replaces `WINEDLLOVERRIDES`, because it already sets
the overrides for so4coop and SO4Fix:

```sh
"/path/to/so4-coop/tools/so4play.sh" %command%
```

The launcher starts the keyboard controller with the game and stops it afterward. To launch without
the keyboard controller, put `SO4_KBPAD=0` before the path. Messages from these two tools are in
Portuguese.

| Keys | Controller |
| --- | --- |
| W A S D | Left stick |
| Arrow keys | Right stick |
| J / K / U / I | A / B / X / Y (Space is also A) |
| Q / E | LB / RB |
| Z / C | LT / RT |
| Left Ctrl | L3 |
| R or Right Shift | R3 |
| T / G / F / H | D-pad up / down / left / right |
| Enter / Backspace | Start / Back |
| Scroll Lock or Pause | Turn the keyboard controller on or off |

## Build
You need clang, lld-link, and llvm-dlltool. MSVC and MinGW are not needed.

```sh
./build_release.sh   # release DLL  -> build/release/xinput1_3.dll
./build.sh           # research DLL with probes and hotkeys -> build/xinput1_3.dll
```

## How it works
The game lets only one character, the leader, take player input; the others are AI. The mod switches
the leader between the active players every 40 ms by default. During each turn, it filters Steam
Input so that only that player's controller reaches the game.

Eight display call sites (mini-map, sight cursor, and target board) are redirected in memory to one
steady character, after each one is checked. For button prompts, `src/texhook.c` hooks the game's own
D3D11 device, identifies textures by the CRC-32C of their top mip (as Special K does), and creates
matching ones from the pack.

Gameplay code isn't patched, game files aren't changed, and save files are never touched. The release
build writes only its own log, `so4coop.log`.

| Folder | Contents |
| --- | --- |
| `src/` | Mod source code |
| `release/` | Settings file and player documentation |
| `tools/` | Linux launcher and keyboard controller |
| `analysis/`, `recon/` | Research scripts |
| `notes/` | Research log ([`estudo.md`](notes/estudo.md), in Portuguese) and playtest logs |

## Credits
- [SO4Fix](https://github.com/Lyall/SO4Fix) by Lyall: the fix this mod is tested alongside.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG: loads SO4Fix.
- [Special K](https://github.com/SpecialKO/SpecialK): the texture pack layout and naming used for
  button prompts.
- [Co-op Anything](https://github.com/cranebeaker/coop-anything): the playbook used to make this mod.
- Developed with the help of Claude.

## License
[MIT](LICENSE). This is an unofficial fan project, not affiliated with or endorsed by Square Enix or
tri-Ace.
