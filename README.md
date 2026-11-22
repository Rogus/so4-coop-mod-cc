# so4coop: local co-op for STAR OCEAN THE LAST HOPE 4K & Full HD Remaster

This mod adds battle co-op for up to 4 players on one PC to the Steam version of *STAR OCEAN THE
LAST HOPE 4K & Full HD Remaster* (app 609150). Each controller drives one party member in battle.
It's a `xinput1_3.dll` proxy written in freestanding C (no CRT), with no game files included.

Tested on: Steam build 2501532, `StarOceanTheLastHope.exe` SHA-256
`284b237259c73c4b02545a2e985eba739b3a68ea365384ebed6666ea2a01a4aa`, Linux with Proton 10.
Windows has not been tested.

## What works (release 1.5)
- Battle co-op for 2 to 4 players: every active player moves, attacks and uses their own skills at
  the same time.
- The mini-map, sight cursor and target board stay steady during co-op (no flicker).
- Tag-team mode (`coop_auto=0`): R3 makes that controller's character the leader.
- PlayStation button prompts without Special K (1.4): Special K texture packs in
  `SK_Res\inject\textures\<crc32c>.dds` are loaded by the mod itself (the pack is not included).
- Join mode (1.5, `coop_join=1`): in a battle each controller takes the first free character at its
  first button press; R3 moves it to the next free one (not yet tried in game). Up to 8 connected
  controllers.
- Keyboard as a virtual Xbox 360 pad on Linux (1.5): `tools/kbpad.py`, started by the Steam launch
  option `tools/so4play.sh`.
- Works alongside Lyall's SO4Fix, renamed from `winmm.dll` to `wininet.dll` and with a 14.40+ C++
  runtime; see [release/README.txt](release/README.txt).

## How it works
The game only lets one character, the leader, take player input; the others are AI. The mod
switches the leader between the active players every 40 ms (`coop_slice_ms`). During each slice it
filters Steam Input so that only that player's controller reaches the game. Eight display call
sites (mini-map, sight cursor, target board) are redirected in memory to one steady character.
Gameplay code is not patched, nothing is written to disk and save files are never touched.
The research log, in Portuguese, is in `notes/estudo.md`.

## Known issues
- Battle only. In the field and in menus, one player leads as usual.
- During co-op the mini-map, sight cursor and target board follow one character
  (`coop_display_pos`).
- The mod starts acting about 45 seconds after the game launches.
- Online play only works through remote-play tools such as Steam Remote Play Together or Sunshine.
  Join mode is made for this, but it has only been tested with local controllers so far.
- QA: manual playtests (rounds 1-48) and self-tests of the controller logic. There is no automated
  in-game test yet.

## Install
See [release/README.txt](release/README.txt) (English) or [release/LEIA-ME.txt](release/LEIA-ME.txt)
(Portuguese). In short: copy `xinput1_3.dll` and `so4coop.ini` next to the exe. On Proton, set the
launch option `WINEDLLOVERRIDES="xinput1_3=n,b" %command%` and keep Steam Input enabled.

## Build
You need clang, lld-link and llvm-dlltool; MSVC and mingw are not needed.

    ./build_release.sh   # release DLL -> build/release/xinput1_3.dll
    ./build.sh           # research build with probes and hotkeys -> build/xinput1_3.dll

## Button prompts (how)
`src/texhook.c` puts a jump on `D3D11CreateDevice(AndSwapChain)` before the game calls them, hooks
`CreateTexture2D` in the vtable of the device the game gets back, names every texture by the
CRC-32C of its top mip (as Special K does) and creates matching ones from the pack's `.dds`.
The research build also lists and saves every texture (`so4coop_textures/`).

## Credits
Made with the help of the [Co-op Anything](https://github.com/cranebeaker/coop-anything) playbook.
