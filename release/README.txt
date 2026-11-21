so4coop co-op 1.4 - STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster (Steam)
=================================================================================

What it does
------------
Battle co-op for up to 4 players. Each controller gets one party member; in battle, every player
moves and fights with their own character at the same time, with their own skills (L2/R2 included).

How: the game itself only lets one character (the "leader") be controlled; the others are AI.
The mod switches the leader between the active players many times per second (every 40 ms by
default) and, during each turn, lets only that player's controller reach the game. A player
counts as active while they use their controller (stick or any button in the last 1.5 s); idle
players' characters are left to the AI, and a single active player plays like single-player.

New in 1.4: PlayStation button prompts without Special K (optional). If the game folder has a
Special K texture pack (SK_Res\inject\textures\<name>.dds), the mod replaces those textures as
the game creates them, just as Special K would, but without Special K. Made for the
"PlayStation Button Prompts" pack (2017), whose only texture is 61a1af48.dds (the main UI sheet).
The pack is not included; get it yourself. Without a pack, nothing changes.

New in 1.3: no more flicker. While co-op runs, the battle mini-map, the sight cursor and the
target board show one steady character instead of switching between the players many times per
second (that switching made the cursors and the characters flash white).

Known issues
------------
- During co-op the mini-map, sight cursor and target board follow one character
  (coop_display_pos, by default the first member) rather than whoever has the current turn.
- Battle only. In the field and in menus every controller works as usual (single leader).
- The mod starts reacting about 45 seconds after the game starts.

Install
-------
1. Copy xinput1_3.dll and so4coop.ini into the game folder (where StarOceanTheLastHope.exe is).
2. Linux / Steam Deck (Proton): Steam -> game Properties -> Launch Options:
       WINEDLLOVERRIDES="xinput1_3=n,b" %command%
   Windows: nothing else to do.
3. Keep Steam Input ENABLED for the game (with it disabled the game gets no controller input).
4. Optional, PlayStation buttons: copy the pack's SK_Res folder into the game folder, so you have
       <game folder>\SK_Res\inject\textures\61a1af48.dds
   Special K itself is not needed (don't install it as well).

With Lyall's SO4Fix (winmm.dll) on Proton, put both DLLs in ONE variable, separated by ";":
       WINEDLLOVERRIDES="xinput1_3=n,b;winmm=n,b" %command%
(Tested together on Linux/Proton 10: co-op, button prompts and SO4Fix all worked.)

Setup: which controller is which (so4coop.ini)
-----------------------------------------------
steamN_position=P   Steam controller N plays party position P (0 = first member).
Steam numbers the controllers in its own order. To find a controller's number: start a battle,
press R3 on that controller and look in so4coop.log (game folder) for a line like
    steamctl ... controller 2 action 10: pressed
Example (3 players): the DualSense is controller 3 and should play the first member:
    steam3_position=0, steam2_position=1, steam1_position=2
The numbering can change if you reconnect controllers; check the log again if characters swap.

Other settings
--------------
coop_auto=1          co-op starts with every battle (0 = off; R3 then works as tag-team:
                     your character becomes the leader).
coop_slice_ms=40     length of each player's turn (smaller = smoother).
coop_idle_ms=1500    after this long without input a player stops getting turns.
coop_active_only=1   0 = every mapped player gets turns even when idle.
coop_display_fix=1   steady mini-map / sight cursor / target board during co-op (0 = off: they
                     follow the current turn and flicker).
coop_display_pos=P   which party position they follow during co-op (default: the lowest
                     steamN_position).
coop_display_sites=bf  advanced: 0f = mini-map only, 8f = + target board, 3f = + sight cursor.
hook_steam=1, leader_only_input=1   needed: each controller only reaches its own character.
tex_hook=1           0 = never load texture packs (button prompts off even with SK_Res present).

Uninstall
---------
Delete xinput1_3.dll, so4coop.ini and so4coop.log from the game folder (and the launch option),
and the SK_Res folder if you added it.

The mod writes the leader position in game memory (the value L1/R1 change), after checking
that a battle is running, filters what each controller sends to the game, and (coop_display_fix)
redirects 8 calls in the game's display code in memory - mini-map, sight cursor, target board -
after checking each one; gameplay code is not changed. With a texture pack it also creates those
textures from the pack files instead of the game's (in memory). Nothing is changed on disk and it never
touches save files.
