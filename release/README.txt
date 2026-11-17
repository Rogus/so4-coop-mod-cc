so4coop tag-team 1.1 - STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster (Steam)
==================================================================================

What it does
------------
Each controller gets one party member. In battle, pressing R3 (right stick click) on your
controller makes your character the battle leader (the one the game lets you control), exactly
like L1/R1 would, but straight to your own character. F3 / F4 on the keyboard do the same for
player 1 / player 2.

New in 1.1: only the leader's controller drives
------------------------------------------------
In battle, the game now only receives input from the controller that owns the current leader;
the other controllers are ignored by the battle (they still work for R3, menus and the field).
So nobody can accidentally move someone else's character. To go back to 1.0 behaviour, set
leader_only_input=0 (or hook_steam=0) in so4coop.ini.

Limits (please read)
--------------------
- The game itself only lets ONE character be controlled at a time (the leader); the others are
  AI. This mod switches the leader; it does not make two characters controllable at once.
- If the leader is a character without a controller in so4coop.ini, every controller drives it
  (no filtering), as in 1.0.
- Battle only. The mod starts reacting about 45 seconds after the game starts.
- R3 also recentres the camera behind the new leader (the game's own R3 action).

Install
-------
1. Copy xinput1_3.dll and so4coop.ini into the game folder (where StarOceanTheLastHope.exe is).
2. Linux / Steam Deck (Proton): Steam -> game Properties -> Launch Options:
       WINEDLLOVERRIDES="xinput1_3=n,b" %command%
   Windows: nothing else to do.
3. Keep Steam Input ENABLED for the game (with it disabled the game gets no controller input).

Setup (so4coop.ini)
-------------------
take_action=10        R3 (the game's Steam action 10). Leave as is.
steamN_position=P     Steam controller N controls party position P (0 = first member).
Which controller is "Steam controller N"? Start a battle, press R3 on a controller and look in
so4coop.log (game folder) for a line like "steamctl ... controller 2 action 10: pressed".
The numbering follows Steam's controller order and can change if you reconnect controllers.

Uninstall
---------
Delete xinput1_3.dll, so4coop.ini and so4coop.log from the game folder (and the launch option).

The mod only writes one byte in game memory (the leader position, the same value R1/L1 change)
after checking that a battle is running; it never touches save files.
