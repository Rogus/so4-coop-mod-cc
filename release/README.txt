so4coop co-op 1.2 - STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster (Steam)
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

Known issues
------------
- With 3 or more players the leader marker flickers: the white highlight on the controlled
  characters and their cursors on the battle mini-map. Gameplay is not affected. A longer
  coop_slice_ms (e.g. 80) flickers less but feels less smooth.
- Battle only. In the field and in menus every controller works as usual (single leader).
- The mod starts reacting about 45 seconds after the game starts.

Install
-------
1. Copy xinput1_3.dll and so4coop.ini into the game folder (where StarOceanTheLastHope.exe is).
2. Linux / Steam Deck (Proton): Steam -> game Properties -> Launch Options:
       WINEDLLOVERRIDES="xinput1_3=n,b" %command%
   Windows: nothing else to do.
3. Keep Steam Input ENABLED for the game (with it disabled the game gets no controller input).

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
coop_slice_ms=40     length of each player's turn (smaller = smoother, more flicker).
coop_idle_ms=1500    after this long without input a player stops getting turns.
coop_active_only=1   0 = every mapped player gets turns even when idle.
hook_steam=1, leader_only_input=1   needed: each controller only reaches its own character.

Uninstall
---------
Delete xinput1_3.dll, so4coop.ini and so4coop.log from the game folder (and the launch option).

The mod only writes the leader position in game memory (the value L1/R1 change), after checking
that a battle is running, and filters what each controller sends to the game. It never touches
save files.
