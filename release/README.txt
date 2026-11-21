so4coop co-op 1.5 - STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster (Steam)
================================================================================

What it does
------------
Battle co-op for up to 4 players. Each controller gets one party member; in battle, every player
moves and fights with their own character at the same time, with their own skills (L2/R2 included).

How: the game itself only lets one character (the "leader") be controlled; the others are AI.
The mod switches the leader between the active players many times per second (every 40 ms by
default) and, during each turn, lets only that player's controller reach the game. A player
counts as active while they use their controller (stick or any button in the last 1.5 s); idle
players' characters are left to the AI, and a single active player plays like single-player.

New in 1.5: join mode (coop_join=1), for playing with friends over the internet through Steam
Remote Play Together or Sunshine/Moonlight (for example BigLinux's Big Remote Play). Guests'
controllers reach the game as more Steam controllers, in an order that changes, so nothing is fixed:
in a battle each controller takes the first free character at its first button press, and R3 moves
it to the next free one. A player who leaves frees their character. Up to 8 connected controllers
(a battle has 4 characters).

New in 1.4.2: xbox_position=P. Steam numbers controllers in its own order, which can change between
sessions. With xbox_position, the Xbox controller (e.g. a keyboard turned into a virtual Xbox 360
pad on Linux) always plays position P, and steamN_position counts only the other controllers. The
mod tells which one is the Xbox controller from the button origins Steam reports for each one.

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

With Lyall's SO4Fix on Proton: as winmm.dll, Proton loads its builtin winmm and SO4Fix never runs
(1.4.1 said they were tested together; that was wrong). Rename SO4Fix's winmm.dll to wininet.dll.
SO4Fix (2024) also needs the C++ runtime 14.40 or newer; the game's prefix usually has 14.0 and the
game closes right after starting. Put a newer (14.40+) msvcp140.dll, vcruntime140.dll and
vcruntime140_1.dll in the game folder, or install Visual C++ 2015-2022 in the prefix. Then:
       WINEDLLOVERRIDES="xinput1_3=n,b;wininet=n,b;msvcp140=n,b;vcruntime140=n,b;vcruntime140_1=n,b" %command%
(Tested together on Linux/Proton 10 on 2026-11-21: SO4Fix.log shows every fix applied.)

Setup: which controller is which (so4coop.ini)
-----------------------------------------------
steamN_position=P   Steam controller N plays party position P (0 = first member).
Steam numbers the controllers in its own order. To find a controller's number: start a battle,
press R3 on that controller and look in so4coop.log (game folder) for a line like
    steamctl ... controller 2 action 10: pressed
Example (3 players): the DualSense is controller 3 and should play the first member:
    steam3_position=0, steam2_position=1, steam1_position=2
The numbering can change if you reconnect controllers; check the log again if characters swap.

xbox_position=P     (1.4.2) the Xbox controller plays position P; steamN_position then counts the
                    other controllers only (steam1 = the first non-Xbox one, and so on). The log
                    shows the result as "controller map: Steam controller N ..." lines.
                    Without this line nothing changes from 1.4.1.

Playing over the internet (Remote Play Together / Big Remote Play)
-----------------------------------------------------------------
In so4coop.ini: coop_join=1. steamN_position and xbox_position are then not used.
- Remote Play Together: the host invites from the Steam friends list with the game running. Guests
  play with a controller (a guest's keyboard reaches the game as a plain keyboard and moves whoever
  has the turn, so use a controller).
- Big Remote Play (BigLinux) / Sunshine + Moonlight: the host shares the screen or the game window;
  Sunshine creates a virtual controller for each guest and Steam sees it as one more controller.
  The game's Steam launch option still applies (Big Remote Play starts the game through Steam).
  Same advice: guests with controllers, not keyboards.
- In a battle each player presses a button to join and R3 to change character. so4coop.log shows
  "=== JOIN: Steam controller N ... joins as position P (name) ===".

Other settings
--------------
coop_join=1          (1.5) join with a button press; R3 = next free character (see above).
coop_auto=1          co-op starts with every battle (0 = off; R3 then works as tag-team:
                     your character becomes the leader).
coop_slice_ms=40     length of each player's turn (smaller = smoother).
coop_idle_ms=1500    after this long without input a player stops getting turns.
coop_active_only=1   0 = every mapped player gets turns even when idle.
coop_press_guard_ms=50  (1.4.1) the turn does not change until this long after a button press, so
                     one press (e.g. an L2/R2 skill) can't also reach the next player's character.
                     0 = off (as in 1.4).
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
