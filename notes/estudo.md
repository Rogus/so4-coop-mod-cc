# Feasibility study: local co-op in STAR OCEAN 4 - THE LAST HOPE 4K & Full HD Remaster (Steam)

## Phase 1 - static reconnaissance (done)
- `StarOceanTheLastHope.exe`: native x64 (tri-Ace's "Aska" engine), DirectX 11, MSVC 2012 (msvcr110).
- Protected: SteamStub (`.bind` section with the entry point) + encrypted code in `.xpdata` (~68 MB),
  decrypted only at run time. We neither remove nor bypass the DRM: the mod works at run time,
  like SO4Fix (which already changes game code at run time without problems).
- Input: `XINPUT1_3.dll` loaded dynamically (LoadLibraryA) -> an `xinput1_3.dll` in the game
  folder is loaded instead (on Proton: `WINEDLLOVERRIDES="xinput1_3=n,b" %command%`).
  There is also the old `SteamController005` API.
- Readable RTTI (5815 types, 545 of them battle). Key classes:
  - `Battle::CPlayerEdge/Reimi/Faiz/Lymle/Backus/Arumat/Melicle/Muria/Sarah`
    -> inherit from `CPlayerThinkChr` -> `CPlayerChr` -> ... (the AI is a layer of the character itself;
       the game already lets you switch the controlled character, so each one has player code and AI code)
  - `Battle::CBtlPad` (controller Task in battle), `Aska::Pad/PadXe/WindowsPad` (controller; "Xe" = Xbox 360)
  - `Battle::CTacticsMode`, `CReplaceMember`, `CMemberBoard`, `CBattleMainTask`
- Vtables are in `.xdata` (not encrypted): the type -> vtable map works (validated in recon/rtti_static.py).

## Phase 2 - runtime probe (in progress)
`build/xinput1_3.dll` (src/probe.c + src/scan.c), read-only:
- passes XInput through to the system's and records which slots (0-3) the game reads and from where in the code;
- F9: finds the characters, CBtlPad, Pad and battle managers in memory, records the
  pointers between them and saves each one's memory in so4coop_dumps/ for comparison;
- F10: marks the log.

Goal: find (1) the "controlled character" variable (comparing F9 with Edge and F9 after switching
characters), (2) whether Aska::Pad reads more than one controller, (3) how CBtlPad delivers commands.

## Feasibility criterion
Prototype: controller 2 moves an ally in a battle while controller 1 moves the leader.

### Run 1 (probe v0.2) - notes/run1_v0.2.log
- The probe loaded through the override in the prefix registry (AppDefaults\StarOceanTheLastHope.exe).
- The game's real code is in the `.data1` section (RX flags); `.text` is tiny.
- The game made ONE XInput call (game+0x3786cd) and did not read controllers through XInput after that:
  with Steam open, it must read through the SteamController005 API (Steam Input).
- Probe bug: F9/F10 were only read during XInputGetState calls -> nothing was captured.
  Fixed in v0.3 (own thread for the keys + logging of the 1st call to each function).

### Runs 2 (v0.3) and 3 (v0.4)
- The game does not read controllers through XInput with Steam open (no XInputGetState): it uses
  `Aska::WindowsPad` (2 instances) fed by Steam Input.
- Party: linked list of characters (`+0x8` previous, `+0x10` next).
- `CBattleMainTask` (battle manager): `+0x90` -> `CBtlPad`; `+0x410` -> object **X**.
  (The `+0x420` seen in run 2 was the tactics menu selection, not control.)
- **Controlled character** (run 3: "Don't do anything" tactic on everyone, switching with L1/R1,
  sequence Edge,Edge,Edge,Sarah,Arumat,Myuria,Edge - exact match):
  block in the character itself, zeroed in those not controlled:
  - `+0x129a0` and `+0x12a20` -> X (the same one pointed to by CBattleMainTask+0x410)
  - `+0x129c0` -> object **Y**;  `+0x129d8` = 120.0f
  - X and Y are recreated on every switch (addresses change).
- Co-op hypothesis: X is the "player operator" tied to the controller; co-op would need
  one X per player, each tied to a different controller.
- v0.5: identifies the class (RTTI) of X, Y and the objects they point to, and copies them.

### Run 4 (v0.5)
- Object X in the controlled character's block is `Battle::CCm146` (family of 60 cameras CCmNNN):
  the block at +0x129a0 is the **camera** attached to the controlled character (+ skeleton joints
  `JointObject`/`HierarchicalObject`). A good indicator of "controlled", but not the input path.
- `CBattleMainTask+0x410` contained garbage this time: not a reliable pointer.
- No object mapped so far (manager, CBtlPad, WindowsPad, tactics, target) points to the controlled one.
- v0.6: searches all writable memory (including the executable's globals) for pointers to each
  party character and identifies the class of the object containing them (s<NN>_refs.txt).

### Run 5 (v0.6) - notes/run5_v0.6
- Edge (F9 #1) -> R1 -> Sarah (F9 #2): the switch was recorded. Camera in this battle: `CCm103`;
  this time X (camera) and Y were **not** recreated on the switch (same addresses).
- Pointer search (analysis/diff_refs.py): 573/591 references; almost all are
  `MessageDispatcherBlockForList+0x60` (recycled message pool = noise).
  Only 6 fields jump Edge -> Sarah:
  - `CCameraManager+0x360`, `CCameraUserCtrl+0xc0` (camera)
  - `CFaceMorphManager+0x540` and a field on the heap next to it (facial expression)
  - **`0x0000000000C2EC00` (low heap, no RTTI)**: the only unexplained candidate.
- Nothing in the input chain (CBtlPad, PadTask, WindowsPad, CBattleMainTask, CTacticsMode)
  points to a character. The 8 KB of CBattleMainTask, CTacticsMode and CSelectTarget were
  **identical** between Edge and Sarah; CBtlPad/WindowsPad/PadTask only changed
  button/stick state and counters.
- Conclusion: input probably picks the character by party **index** or through an
  object outside those mapped; a pointer search does not find an index.
- Next (v0.7): write breakpoint (debug registers) on field 0xC2EC00 and on
  CCameraUserCtrl+0xc0 to record the instruction that switches the character when R1 is pressed ->
  that function is the "controlled character switch", the real lever for co-op.
- v0.7 (src/watch.c): after two F9s with different characters, arms up to 4 hardware
  watchpoints (read/write, 8 bytes) on the fields that jumped to the new character; a VEH
  records each game instruction that reads/writes them and saves the function's code in
  so4coop_dumps/code_<rva>.bin. F11 = report, and removes the watchpoints. Self-test on local
  Wine 11.22: works (Dr7 read back, DR6 filled in). On Proton, the self-test goes at the start of the log.

### Run 6 (v0.7) - notes/run6_v0.7
- Self-test on Proton 11.0-100: hardware watchpoints **work**, but DR6 comes back empty
  (attribution only by value change; reads show as "w?").
- 4 watchpoints armed in 61 threads (Dr7 = 0xBBBB0055, correct), **0 accesses** in 4 min.
- Cause: the "heap fields without a class" at 0xC2xxxx (including 0xC2EC00 from run 5) are the
  **probe's own stack**: F9's local variables hold the controlled character; three
  of them already had another value by the time they were armed. A probe artifact, not the game's.
- Without that artifact, only camera objects follow the switch (CCameraManager+0x360,
  CCameraUserCtrl+0xc0, CCameraAngleLock+0x190) and CFaceMorphManager+0x540.
- find_controlled.py on run 3 (7 captures): the 46 candidates are all in the camera
  block (+0x129a0..+0x12a24): the character has **no** "controlled" flag of its own.
- v0.8: ignores the probe's stack (logs the range), watches the camera fields
  (whatever writes them on a switch must be the character switch routine), does not rearm on later
  F9s, limits unattributed reads, and saves the game's global variables on every F9
  (to look for a party index: Edge 0, Sarah 1, Arumat 2...).

### Run 7 (v0.8) - notes/run7_v0.8
- Probe stack = 0xB30000-0xC30000: **confirmed** that the 0xC2xxxx fields were the probe itself.
- Captures: #1 Edge, #2 mid-switch (none controlled), #3 Sarah, #4 Edge, F11, #5 Edge.
  v0.8 bug: #2, with none controlled, erased the previous reference (fixed in v0.9).
- Watchpoints on CCameraManager+0x360 and CCameraAngleLock+0x190: 0 accesses (no switch after arming).
- F11 (removing watchpoints = suspending the 61 threads again) -> the game froze; the probe stayed alive.
- **Globals (.arch, 7.4 MB)**: only one block is the same in Edge's 3 captures and different in Sarah's:
  - `game+0xf77ed2` Edge 2 / Sarah 3; `game+0xf77ed3` Edge 1 / Sarah 2; `game+0xf77f00` Edge 2 / Sarah 3
  - capture #2 (mid-switch) already has Sarah's values: it changes before the camera.
  - Strong candidate for "index of the controlled member". A 3rd character is needed to see the numbering.
- v0.9: arms fixed watchpoints once, on the 1st F9 in battle: the two globals, CBtlPad+0x1180
  (control: always changes) and CBtlPad+0x28 (stick values); F11 only reports, does not disarm.

### Run 8 (v0.9) - notes/run8_v0.9
- Run 8's log was **overwritten** by the next launch (white screen); only the dumps
  remain (2 captures, 01:12:04 and 01:12:08) -> probable freeze right after arming the watchpoints.
- In both captures Sarah is the controlled one, but the global block has `02 01` (Edge's values in
  run 7): the number is **not the character**; it looks like the controlled one's position in this
  battle's party order (leader = 1 at +0xed3). +0xf00 dropped to 0 in the 2nd capture: it is something else.
- Launch with a white screen (01:15): the probe loaded, watchpoint self-test ok, XInput calls,
  and the log stops (no 30 s heartbeat).
- Hardware watchpoints on Proton = 2 (maybe 3) freezes: **turned off in v0.10**.
- v0.10: log in append mode with a session header; previous so4coop_dumps renamed;
  static search in the decrypted code (RIP-relative displacements and absolute addresses) for
  every instruction that points to game+0xf77e80..0xf77f40, with a dump of the functions.
- Sound: PulseAudio and PipeWire/WirePlumber running together; WirePlumber holds the reservation
  (org.freedesktop.ReserveDevice1) of the 4 cards, so PulseAudio's outputs (which Proton uses)
  stay suspended. Not a problem of the game or the probe.

### Run 9 (v0.10) - notes/run9_v0.10
- Launch options: with `WINEDLLOVERRIDES=xinput1_3=n,b` the game stopped before the title
  (gray screen + 1280x720 black, sleeping threads, no dialog). With only `DXVK_FRAME_RATE=60 %command%`
  it started normally. The override in the prefix registry is enough to load the probe.
- Sound: solved by replacing PulseAudio with pipewire-pulse (WirePlumber was holding the cards).
  analysis/live_check.sh measured the game's audio on HDMI 2: peaks from -17 to -0.2 dBFS (real sound).
- Edge (#1) -> Arumat (#2): the global block was `02 01 / 02` in both -> it is **not** the controlled
  member (run 7's coincidence was following something else). Hypothesis discarded.
- 14 static references to the range; 10 read game+0xf77ea8 = global pointer to an allocator
  (aligns an address and calls methods +0x58/+0x70 of the object at +0x2a8). Unrelated to control.
- Solid fact that remains: only the controlled character has the camera block at +0x129a0.
  v0.11: saves the whole code section once (68 MB, stays on disk, never in git) to search
  offline for what writes +0x129a0 and climb the callers up to the character switch routine.

### Run 10 (v0.11) - notes/run10_v0.11
- Code section saved: game+0x013a2000-0x05829932 (71.8 MB, 0 unreadable pages), matches byte for
  byte the function saved in run 9. Stays on disk only (code_full_013a2000.bin), never in git.
- analysis/static_xref.py: offline search for displacements (disp) and direct calls (E8),
  with function boundaries from the exe's .pdata (not encrypted) and disassembly by objdump.
- **No instruction uses 0x129a0/0x129c0/0x12a20** as a displacement from the character; the
  20 nearby hits (0x12984, 0x12998...) are false (jump bytes or data).
- The camera block is a record without a vtable at chr+0x12998 (1st field = owner), right before an
  embedded CFaceMorphManager (vtable at chr+0x12a28). Only the camera system touches it, by pointer.
  A symptom of the switch, not the mechanism. It also disappears a few seconds later (Arumat's dump = 0).
- UI classes that need to know who is controlled: CArrowCursorManager,
  CMinimapPlayerMarkerElement, CMemberBoardChangeArrowElement. None shows up in the pointer
  searches -> the controlled one must be stored as an **index**.
- Next: v0.12 with the RTTI map of all Battle:: classes (not just 24), a dump of all
  instances per F9 and a diff of integer fields that follow the sequence Edge, Sarah, Arumat, Edge, Sarah.

### Run 11 (v0.12) - notes/run11_v0.12
- 542 Battle:: classes, ~1210 instances per F9 (4.8 MB, < 1 s). Captures: Edge, Arumat, Edge.
- diff_battle.py (min. 2 characters): 4458 candidate fields; with small values (0-7) and the
  same mapping in independent objects, two remain:
  - `CBattleMainTask+0x3c4` Edge 1 / Arumat 3 (right after +0x3c0 = 3 constant)
  - `CStatusBoardWindow+0x1f0` Edge 1 / Arumat 3, followed by the position of the highlight on the panel:
    x = 353 (Edge) / 919 (Arumat), y = 544 -> 283 px per position: **party position, base 1**
    (Edge 1, Sarah 2, Arumat 3, Myuria 4). Prediction: Sarah 2 / x 636, Myuria 4 / x 1202.
- Global pointers to CBattleMainTask: game+0xf76940 and game+0x1051840 (no direct
  RIP-relative access in the code).
- The only access with displacement 0x3c4 (game+0x3704a30) writes the constant 2 to an object passed
  as an argument (another class?). Parts of the code have anti-disassembly garbage after unconditional
  jumps: the jumps must be followed, not read in sequence.

### Run 12 (v0.12) - notes/run12_v0.12
- Captures: Edge, Sarah, (mid-switch). **Prediction confirmed for Sarah**: +0x3c4 = 2 and
  panel highlight at x = 636.
- Adjustment: Edge came out as 0 (x = 70) in this battle and 1 (x = 353) in run 11. The 5 points follow
  x = 70 + 283 × position. So `CBattleMainTask+0x3c4` = **position of the controlled member in this
  battle's party order, base 0 (0-3)**, not a fixed number per character; +0x3c0 = 3 = highest
  position. `CStatusBoardWindow+0x1f0` is the copy the UI uses.
- The mid-switch capture already has the new value: the field changes before the camera.
- Next: (1) find the list of members in this battle's order (position -> character);
  (2) write test: the probe writes +0x3c4 and we see whether control switches without R1 (first
  write to the game's memory: only with explicit authorization).

### Run 13 (v0.13) - notes/run13_v0.13
- Write test (F8): **refused, nothing was written**. CBattleMainTask+0x3c0/+0x3c4 = 0 / 256 in this
  battle and they do not follow the switch (captures Edge, Edge, Arumat, Edge): the coincidence in
  runs 11-12 was another state. **Do not use v0.13's F8** (wrong target; disable in the next version).
- CStatusBoardWindow+0x1f0 is still consistent (x = 70 + 283 × position), but in capture #2 it already
  showed 1 with the camera still on Edge (same as run 12's mid-switch capture): the panel changes **when
  R1 is pressed** and the controlled character only changes after the animation. The panel shows the
  requested/destination member.
- Static: 6 functions use +0x1f0 and +0x1f4; the "setter" game+0x3b7e460 writes 4 integers to
  +0x1f0..+0x1fc (last = 0) and has no direct calls (virtual); it does not match the panel's layout.
- Next: automatic capture (the probe detects the switch by the camera and captures by itself after 1.5 s
  stable) to get 4 characters without depending on F9 timing.

### Run 14 (v0.14) - notes/run14_v0.14
- 1st session closed by the user (23 s). 2nd: F7 turned on automatic capture, but **no captures**.
- Cause: the block at +0x129a0 is **not the controlled character**; it is the camera focus, which wanders
  among the members by itself. In the dumps made ~3 s after each "CONTROLLED" line in the log it was already on
  another member or on none (run 11: log Edge/Arumat/Edge, dumps Sarah/Myuria/Sarah; run 12:
  log Edge/Sarah/-, dumps -/Arumat/Edge; run 13: log Edge/Edge/Arumat/Edge, dumps Edge/-/Myuria/Edge).
  The "controlled" labels in runs 3-13 came from it and are not reliable.
- What directly follows R1 is the panel (CStatusBoardWindow+0x1f0, x = 70 + 283 × position).
- v0.15: automatic capture triggers on and labels by the panel position (stable 1.5 s); the camera
  focus is kept only as a reference; diff_battle.py uses the "POSITION: p" lines.

### Run 15 (v0.15) - notes/run15_v0.15
- F7 turned on twice (panel at position 1, then 0), **no captures**: with samples every
  250 ms the panel position never stayed the same for 1.5 s straight -> it also changes by itself
  (animates or cycles through the members), not only on R1.
- v0.16: samples every 100 ms, records each change with the time and the camera focus (up to 400
  lines) and captures when one position takes up >= 80% of the last 15 samples.
- User's observation in run 15: the character actually controlled **changes together with the panel
  highlight** when R1 is pressed. The panel is a good indicator of the controlled member.

### Run 16 (v0.16) - notes/run16_v0.16 — **first solid result**
- Automatic capture worked: 8 captures, two rounds, positions 1,2,3,0,1,2,3,0. The panel changed
  exactly once per R1 (it does not keep changing by itself).
- diff_battle.py (4 positions, each one 2 times): 158 fields, with a clear structure:
  - **`CBattleMainTask+0x3a4` = position of the controlled member (0-3)**, `+0x3a0` = highest position (3).
    The block +0x3a0..+0x3ac (16 bytes) is copied by a getter at game+0x3c7adb0.
    (The +0x3c4 in runs 11-12 was a sibling block 0x20 further on: coincidence.)
  - **Pointer to the controlled character** at `CThisIs+0x38`, `CTargetSign+0x298`,
    `CHitCounter+0x528`, `CCameraManager+0x390`, `CCameraAngleLock+0x190`.
  - Position -> character in this battle: 0 Edge, 1 Sarah, 2 Arumat, 3 Myuria (party order).
  - Several UI elements copy the position and the highlight's x (70/353/636/919).
- Static: only 3 functions use displacement 0x3a4 and 2 use 0x3a0; the setter of CBattleMainTask's
  block does not show up by direct displacement (it writes by pointer or an embedded copy).
- Next: write test on +0x3a4 with a cross-check (panel and CThisIs) to see whether that
  field alone switches control; then find how controller input reaches the character.

### Run 17 (v0.17) - notes/run17_v0.17
- F8: checks 1 (+0x3a0 = 3, +0x3a4 = 0) and 2 (panel = 0) passed; **check 3 failed** (the first
  CThisIs found did not point to a party character according to the list of 24 classes) -> **nothing written**.
  The user reported "yes it does" (the highlight/camera changed?), but it was not the probe's doing.
- v0.18: check 3 goes through all CThisIs instances and identifies the character by RTTI
  (class_of), without depending on the list limited to 256 objects; if it fails, it logs each instance and its target.

### Run 18 (v0.18) - froze before the title
- Gray screen + 1280x720 black again (02:42), **with only `DXVK_FRAME_RATE=60 %command%`**: the hypothesis
  that WINEDLLOVERRIDES caused this was wrong. Third freeze (01:15, 01:27, 02:42) in ~20
  launches, all with the probe. No processes left over from the previous session; sleeping threads, silent
  audio; the probe's heartbeat continued.
- Suspicion: the probe's thread calls GetAsyncKeyState every 30 ms from 0.5 s after loading, at the same time as
  the creation of the game's window/device (possible race in Wine).
- v0.19: no keyboard calls in the first 45 s ("hotkeys active" in the log). If it freezes again,
  the next step is to launch without the probe (WINEDLLOVERRIDES=xinput1_3=b) to rule it out.

### Run 19 (v0.19)
- Started normally (no freeze; keyboard only after 45 s). One run does not prove that was the cause.
- F8: **check 1 failed**, nothing written: CBattleMainTask+0x3a0 = 1083249128 (looks like float ~4.5), +0x3a4 = 1.
  Either it was not in active combat at that moment, or the block changes from battle to battle. Next: F8 and
  then F9 in active combat, to see the whole block at that moment.
- **Game mechanic (user's report):** in battle only the **leader** is controlled; L1/R1 switches the
  leader; the camera and focus follow the leader; the other members are always AI. So +0x3a4 = position of the
  **leader** in the party. For co-op: player 2's member must receive input from controller 2
  instead of the AI, without becoming the leader. Next targets: where controller input is applied to the leader and
  where the AI decides the other members' actions.

### Run 20 (v0.19) - notes/run20_v0.19 — **leader found reliably**
- F8 refused (main+0x3a0 = 0 / +0x3a4 = 0); the F9 right after showed that the region main+0x380.. had
  other content: CBattleMainTask ends before +0x3a0 and the 4 KB dump was reading its neighbor.
- **`CBattleMainTask+0x68` points to the leader block**: [0] = highest position (3), [4] = leader's
  position; +0x60 = common base (state stack?). The block moves around (main+0x3a0, +0x3c0, +0x3e0).
- Validated in **19 captures from 5 battles** (runs 11, 12, 13, 16, 20): always max = 3 and leader = panel.
  It explains everything: the "+0x3c4" of runs 11-12 was this block at +0x3c0; run 13's "0/256", a wrong fixed read.
- CThisIs/CTargetSign/CHitCounter are not stable (in run 20 they point to CThisIs or to 0).
- v0.20: F8 writes 1 byte at [block+4] only if: (1) the block is valid via +0x68 (up to 0x200 after +0x60, max 1-3,
  leader <= max); (2) panel = leader; (3) max + 1 = no. of party characters in memory (RTTI).
  The 3 checks pass in all 19 captures.
- Short sessions at 03:02 (30 s) and 03:02:53: extra launches, no data.

### Run 21 (v0.20) - notes/run21_v0.20 — **first write, and the game went along**
- The 3 checks passed; 5 writes (4 in the 1st battle: 0->1->2->3->0; 1 in the 2nd: 0->1).
- After each write: the value **stayed** (3 s), the **panel highlight slid** to the new position
  (x 70 -> 295 -> 353 etc., the same animation as R1) and the **camera focus went to the new leader**
  (Sarah, Arumat, Myuria, Edge). CThisIs pointed to no one. No freeze.
- In the 2nd battle the block was in another region (0x149B4D3A0, base +0x60 = 0x149B4D350): the pointer
  at +0x68 works across battles.
- User's report still needed: did the stick start controlling the new leader?
- **User's report:** after each F8 the stick started controlling the new leader. So **1 byte
  ([*(CBattleMainTask+0x68) + 4]) switches the leader completely** (camera, panel and control), without freezing.
  As expected, there is still only one controllable character: that is the game's design (1 leader + 3 AI).
- Co-op requires a 2nd member (not the leader) driven by controller 2 instead of the AI. Next targets:
  (1) how controller input reaches the leader (start with CBtlPad's methods, vtable game+0x9d6c78,
  offline); (2) where the AI decides the other members' actions; (3) reading the 2nd controller (Steam Input,
  2 WindowsPad objects).

## Phase 3 - controller input (offline, code saved in run 10)
- analysis/vtable.py: image = exe on disk + saved .xpdata section; lists the vtable and disassembles the start
  of each method (stops at ret/jmp because of the anti-disassembly garbage). Output in notes/cbtlpad_vtable.txt.
- **CBtlPad** (vtable game+0x9d6c78): 19 methods, most of them stubs. [12] (game+0x2f9bae0) goes through 2
  records of 0x14 bytes at this+0x28 (float counter + flag at +0x10) and, on reaching zero, calls
  game+0x75c840 -> **rumble timer for 2 controllers** (the 32767.0 seen in run 5).
- **Input layer**: global table of **2 controllers** at game+0x1059634 (records of 0x6e8 bytes,
  1st byte = id 0 and 1). game+0x75c660(id) looks up the record; if the id does not exist, it ORs the buttons of
  record 1 into record 0 and returns a default record -> the game reads the 2 controllers **separately** and
  **merges** them for a single player.
- 43 direct calls to game+0x75c660: most ask for controller 0; 4 functions ask for 0 and 1
  (game+0x7b1510, 0x7b2e60, 0x7b7090, 0x7d2870). All in .data1 (not encrypted on disk).
- Next: find out which controller 0 call feeds the leader in battle; for co-op, the 2nd member
  would need to receive controller 1's data (requires changing code at run time, as SO4Fix does).
- analysis/callgraph.py: RTTI of all classes (5201 classes, 3235 vtables, 10091 labeled
  functions) + index of direct calls (122743) -> labels who calls a function, climbing up to virtual
  methods. Outputs: notes/getpad_callers.txt and notes/getleader_callers.txt.
- Callers of game+0x75c660 (get controller): almost all from menus (Camp), field (Field), title and
  saving. In battle only three:
  - game+0x4fd740 = **StartRumble(CBtlPad, controller, motor1, motor2, duration)**; called by
    CPlayerChr::vf107 only when the character is the leader (rumbles controller 0 at 0x7fff).
  - game+0x5152c0 / 0x515690 (CKernel@Battle::vf12): go through the 2 controllers and reset device
    state, then query the leader (flag leader+0xd46, subsystem at *(main)+0xb0): looks like
    setup/pause, not per-frame movement.
  - CBtlPad::vf12 (rumble timer).
- **Definition of the leader in the code** (confirms what we found at run time):
  - game+0x4f7310(main) = `return *(main+0x68)` (leader block)
  - game+0x4f9990(party, i) = `return *(party + 0x5a0 + 8*i)`, party = *(CBattleMainTask+0x58)
  - **game+0x5000d0 = GetLeader(system)** = party->members[block[4]]; system = [game+0x1051838]
    (1st field = CBattleMainTask). 109 functions call it.
  - **game+0x53e390 = IsLeader(character)** = (GetLeader(system) == character): the question "am I the
    one controlled by the player?" used by dozens of methods of CPlayerChr/CPlayerThinkChr.
- Co-op hypothesis: make IsLeader answer "yes" for player 2's character too and give
  it controller 1's input (the leader's movement does not seem to come from game+0x75c660; the per-frame
  input interface is still to be found). This requires changing code at run time: ask for authorization first.
- analysis/users.py: what comes right after each call to a function. In the 31 uses of IsLeader what
  follows is behavior (rumble, voices, command dispatcher game+0x500b40 with a 10-case table,
  party/character data), **not** the controller read.
- References to game+0x1059000..0x105a500 (625 in 337 functions, notes/input_globals_xref.txt): the area
  game+0x1059e78..0x1059ea0 is an event/listener registry (bitmask per category + id lists,
  game+0x762e40), not the controller; the "vf142" flagged was a false positive (bytes inside calls).
- Conclusion: static analysis found **who the leader is** (GetLeader/IsLeader), but not **where movement
  comes in** (it must go through virtual methods of the controller objects). Next: stick-driven
  capture (neutral/up/right/down/left, repeated) and diff, as was done with the leader.
- Offline validation of the leader formula: party = *(CBattleMainTask+0x58) = **CChrManager@Battle**,
  members at +0x5a0 = [Edge, Sarah, Arumat, Myuria] (run 16), leader = members[block[4]] matches
  in all captures (runs 16 and 20).
- v0.21: F6 = capture labeled by stick (neutral, up, right, down, left, ×2), with Battle::
  objects, the 24 classes (characters 256 KB, WindowsPad), input globals game+0x1059000..0x105b000
  and the leader found by the game's formula. analysis/diff_stick.py looks for X/Y axes and discrete fields
  (tested with synthetic data).

### Run 22 (v0.21) - notes/run22_v0.21 — **stick found in the globals**
- 7 of 10 captures (neutral, up, right, down, left, neutral, up); leader Edge; no freeze.
  Before that there was a short launch (03:42, ~30 s), and the game closed ~30 s after the 7th capture.
- diff_stick.py (strict criterion) found no axis in the character, the WindowsPads, PadTask or CBtlPad:
  the 2 WindowsPads hardly change. "Up" came out skewed (1st far to the right, 2nd already released).
- In the input globals (game+0x1059000+):
  - `+0x476` (s16) = **left stick X**, `+0x478` (s16) = **Y** (raw −32768..32767):
    right 32732/−1499, down 644/−32760, left −32362/5126, neutral 0/0.
  - copy at `+0x64a/+0x64c` = **controller 0 record** (game+0x1059634) **+0x14/+0x18** ->
    each record has its own stick; controller 2 should be at game+0x1059d30 (record 1 + 0x14).
  - `+0x474` = game+0x1059080 + 0x3f4: **merged** state that the game uses.
  - direction bits at +0x6d0..+0x6d8 (right 8, down 2, left 4) and counters at +0x690..+0x698.
- Who reads it: no direct reference to those addresses; game+0x1059080 is only used by SozApp,
  AppSetupTask (setup), PadTask@Peripheral::vf12 (per-frame update = the one that **fills it in**) and
  PadTask's constructor (game+0x75bf90). No captured object holds a pointer to that area.
  The consumer (leader movement) does not show up in static analysis.
- v0.22: F5 = guard-page trace (2 s) of page game+0x1059000: each access lands in the VEH, which
  records it (instruction, offset, read/write), lets the instruction run with TF and rearms the guard
  on the next step; then restores the protection. No suspending threads, no ptrace, no writing data.
  Self-test on local Wine (a page of the probe itself, 400 reads): 400 accesses, 2 instructions -> works.

### Run 23 (v0.22) - notes/run23_v0.22 — **guard page crashed the game**
- F5 turned on the guard on page game+0x1059000 (protection 0x8); the log stops at "GUARD TRACE ON" (no OFF,
  no report, no heartbeat) and the game ended at 04:01:27: it died within the 2 s window.
- Probable cause: code outside Windows exception handling (the native side of Steam Input /
  lsteamclient writing to the controller records) touched the guarded page. **Do not use v0.22's F5.**
- Static search by displacements 0x3f4/0x3f6/0x3f8 (stick field from game+0x1059080):
  too much noise (common offsets and badly decoded instructions in the obfuscated code).
- Leader object (CPlayerEdge, 256 KB) in run 22: no float ~0 at neutral and different at
  right/down/left -> movement intent is not in the character's first 256 KB.

## Resuming (after run 23)
- v0.23 installed (F5 disabled). Commit 6333dec.
- PadTask@Peripheral::vf12 (game+0x4e594d0) calls game+0x75c1b0 = **update of one controller**
  (notes/pad_poll.txt): the object has the device at [obj], ORed buttons at +0x5bc/+0x5be/+0x5c2,
  sticks at +0x5ca/+0x5cc (left) and +0x5ce/+0x5d0 (right), triggers +0x5d2/+0x5d3.
  With obj = game+0x1059080 this matches controller 0's stick (game+0x105964a): there are **two
  controller objects** of 0x6e8 bytes, at **game+0x1059080** and **game+0x1059768**; the "records" are
  their state section at +0x5b4; the "merged" game+0x1059474 = object 0 + 0x3f4.
- No global points to those objects; the header game+0x1059000..0x105907f has dozens of small
  fields read by many classes, none clearly the base of the stick.
- Loose search over everything from run 22 (1221 objects + 24 classes + globals): no float ~0 at neutral
  and nonzero at right/down/left. Static analysis and diffs exhausted for "who reads the stick".
- Next tool proposed: **software breakpoint** (int3) on GetLeader/IsLeader, with the probe's
  handler recording return addresses and stack for 1 s, standing still vs walking; it touches the
  game's code in memory (code page protection), ask for authorization.

## Tag-team mode (v0.24) — first playable mod
- src/tagteam.c: each player "owns" a party position (default: player 1 = 0, player 2 = 1;
  optional so4coop.ini with p1_position= / p2_position=). **L3 on your controller** (or F3 / F4 on the keyboard)
  makes your character the leader, by writing the byte in the leader block (as in run 21).
- Controllers read through the real XInput (own polling every 30 ms, slots 0-3); the first two connected
  slots = players 1 and 2; the log shows the slots seen.
- Checks before writing, without a full scan: cached CBattleMainTask (verified by RTTI),
  valid leader block, *(main+0x58) is a CChrManager with members[0..max] all characters, target exists.
- Limit: the game still merges the two controllers, so both move whoever is the leader.
- Local test (system Wine): ini read (2/3), slot 0 connected (DualSense) -> player 1, no player 2;
  outside battle "no battle (nothing written)".

### Run 24 (v0.24) - notes/run24_v0.24 — tag-team by keyboard works
- **F3/F4 worked**: 7 Edge <-> Sarah switches with all checks, the game ran 6 min without problems.
- **L3 cannot work yet**: XInput sees no controller on Proton ("slots connected" empty).
  The game reads controllers through the Steam Input API; Proton does not expose them to XInput.
- Fix: read the buttons from the **game's own controller objects** (game+0x1059080 and game+0x1059768,
  button words at +0x5bc..+0x5c6; game+0x75c660 merges the 6 words of controller 2 into 1). Still to
  find out which bit is L3: a calibration round recording the changes in those words.
- v0.25: tag-team reads the buttons from the game's controller objects (6 words at +0x5bc of
  game+0x1059080 / game+0x1059768) every 30 ms; records every change (up to 600 lines) to calibrate;
  switch button configurable in so4coop.ini: take_word=<0-5>, take_mask=<hex>. XInput remains as an
  alternative. Local test: ini read (word 2, mask 0x40); outside the game the objects do not exist and are ignored.

### Run 25 (v0.25) - notes/run25_v0.25 — calibration
- 237 button changes, **all in controller object 1** (pad1); object 2 (game+0x1059768) never changed.
  With Steam Input on, both physical controllers seem to land in the same game object (or the 2nd was not used).
- Layout of the 6 words: [0] = held, [1] = previous frame, [3] and [5] = just pressed.
  Bits pressed: 0x0001 x3, 0x0004 x2, 0x0008 x1, 0x0080 x11, 0x0400 x8, 0x0800 x4, 0x1000 x8,
  0x2000 x4, 0x4000 x36, 0x8000 x3 -> R3 not identifiable (many other buttons in the session).
- L3 = target lock; R3 = recenters the camera on the controlled one (acceptable for switching players).
- v0.26: the XInput path uses R3 (0x0080). Proposal: **turn Steam Input off for this game only**; then
  Proton's XInput sees each controller in its own slot and the mod can tell players 1 and 2 apart.

### Run 26 (v0.26) — Steam Input off
- The game ran 67 s; the mod's XInput still had no controllers, the game did not call GetState and the game's
  controller objects did not change: **without Steam Input the game gets no controller** (turn it back on).
- The exe only references XINPUT1_3.dll (check/rumble) and "SteamController005": the game reads input through
  **ISteamController**. There is no Steam action file for the game on disk; the action names do not
  appear in the exe or in the decrypted code.
- v0.27 (src/steamctl.c): gets the interface through the game's steam_api64.dll (SteamClient ->
  SteamAPI_ISteamClient_GetISteamController(..., "SteamController005")), lists the physical controllers
  (GetConnectedControllers) and queries actions 1..32 on each one (GetDigitalActionData) every 60 ms,
  recording each change (calibration). take_action=<n> in so4coop.ini: action n on Steam controller k
  -> player k takes over.

### Run 27 (v0.27) - notes/run27_v0.27 — Steam sees 3 controllers
- SteamController005 interface obtained; **3 controllers connected** (handles 0x1C666D5EC4E5, 0x135371001869A2DE,
  0x2D90895F4DC78F). 130 changes, **all on Steam controller 3**: action 1 x1, 2 x2, 3 x45, 4 x2, 10 x15.
  Controllers 1 and 2 reported no actions.
- Hypothesis: Steam only reports actions where the game activated its action set; the game seems to use only one
  controller (the 3rd), which also explains everything being in controller object 1 in run 25.
- Action 10 = candidate for R3 (15 presses at the end); action 3 = probable attack.
- Next: confirm with the user the order and whether the 2nd controller did anything in the game; if not, activate the
  current action set (GetCurrentActionSet of controller 3 -> ActivateActionSet on the others) to read them.
- User's report (run 27): R3 3x on the DualSense, then 3x on the other; quit with Super+Q. Steam's 3rd
  "controller" is not the keyboard (F3/F4 are read directly by the mod); probably a virtual/duplicate device.
- v0.28: time on each action line; per controller, gamepad index and action set; activate_all=1
  (so4coop.ini created in the game with only that line): every 2 s the action set of the controller that has one is
  activated on the others. take_action is now decimal (the test caught 10 -> 16 with the hex reader).

### Run 28 (v0.28) - notes/run28_v0.28 — **each controller separated by Steam**
- 3 Steam controllers, all already with the game's action set 1 (activate_all did not need to activate anything);
  gamepad index -1 on all of them.
- **Action 10 = R3**, seen on each controller separately: controller 3 (61-72 s, 3x), controller 2 (77-98 s, 4x),
  controller 1 (104-121 s, 4x). The DualSense (pressed first) should be controller 3; the other physical controller
  seems to appear as two Steam entries (2 and 1). Action 3 x43 every ~0.3 s = held button repeating.
- v0.29: p1_steam=<n> in so4coop.ini = player 1's Steam controller; any other controller = player 2.
  Proposed so4coop.ini: take_action=10, p1_steam=3, activate_all=0, p1_position=0, p2_position=1.
- User's controllers: **DualSense, GameSir G7 (Xbox One style) and DualShock 4 v1** = Steam's 3 controllers.
  In run 28 R3 was pressed on all three: controller 3 = DualSense; 2 and 1 = GameSir / DualShock 4 (order to be confirmed).
- v0.29 (final): steamN_position=<0-3> in so4coop.ini gives each Steam controller its party position
  (up to 4 players); without those lines p1_steam (player 1) / all the others (player 2) applies. tt_take split
  into tt_take_pos(label, position). Local test: positions read (3->0, 2->1, 1->2), refusal outside battle.
  The order of the controllers in Steam can change between sessions (the log lists it on every launch).

### Run 29 (v0.29) - notes/run29_v0.29 — **tag-team by controller working**
- R3 (action 10) on each controller switched the leader to the configured character, two rounds:
  controller 3 -> Edge, controller 2 -> Sarah, controller 1 -> Arumat (6 presses, all checks ok, no freeze).
- Still needed from the user: which physical controller is 2 and which is 1 (GameSir / DualShock 4), and whether R3 did
  anything besides recentering the camera.

## Release: so4coop tag-team 1.0
- build_release.sh compiles the same sources with SO4COOP_RELEASE: only F3/F4 + R3 through Steam's controllers,
  log in so4coop.log. Package build/release/so4coop-tagteam-1.0.zip (xinput1_3.dll, template so4coop.ini,
  README.txt, LEIA-ME.txt; the package's source files in release/). Tested on local Wine.
- User's request: the other controllers playing the other members **at the same time** in battle.
  Not possible yet: the game has only one controlled character (leader) and we do not know how to inject commands
  into the other members' AI. Proposed next step: intercept the game's calls to ISteamController
  (the interface's virtual table) to separate input per controller (only the leader's controller drives; the
  others' input stays available to feed another character) and then study the AI's "think".

## Phase 4 - separating input per controller
- Release 1.0 installed in the game (xinput1_3.dll f244b3d6…), user's so4coop.ini kept.
- Disassembly of the flat thunks in the game's steam_api64.dll: RunFrame +0x10, GetConnectedControllers +0x18,
  ActivateActionSet +0x30, GetCurrentActionSet +0x38, GetDigitalActionData +0x48, GetAnalogActionData +0x60.
  The two "Get…Data" are MSVC methods that return a struct through a hidden pointer: (this=rcx, ret=rdx,
  controller=r8, action=r9) -> rax = ret. Analog = {int mode; float x; float y; bool active} (16 bytes).
- v0.30 (src/steamhook.c, hook_steam=1 in the ini): copies the interface object's virtual table, replaces those
  6 methods with wrappers that only count and pass through, and points the object to the copy (one 8-byte write
  to the Steam object; no code changed). Report every 10 s (2 min) and then every 60 s: calls
  per method, per controller handle (or "all"), actions queried and who called in the game. Calls from the
  probe itself do not count.
- Self-test on local Wine with a fake object of the same ABI: correct counts, correct pass-through (digital and
  analog), own calls ignored. The release also has the code, off by default.

### Run 30 (v0.30, hook_steam=1) - notes/run30_v0.30 — **the game queries per controller**
- Hook stable (~2.5 min, with tag-team by R3 working alongside).
- In 100 s: RunFrame 30864, GetConnectedControllers 12346, **GetDigitalActionData 889056 = 296352 on each of the
  3 handles**, **GetAnalogActionData 74088 = 24696 per handle**. No calls with "all controllers".
  Digital actions 0..16, analog 1 and 2.
- Callers in the game (Aska input layer): game+0x7a535a (GetConnected), 0x7a556d and 0x7a55a3 (digital),
  0x7a55f8 (analog), 0x7a5c59 (RunFrame). The game merges the controllers afterward.
- So: the wrapper can neutralize the controllers that are not the leader's (released state, zeroed stick) and
  each controller's input stays available to the mod (the probe's own queries are not filtered).
- v0.31: leader_only_input=1 (with hook_steam=1): in the wrappers, the game's queries on a controller that is not
  the leader's get "released" / zeroed stick (bActive kept); the probe's own queries are never
  filtered (everyone's R3 keeps working). The leader's controller is recomputed every 60 ms: the leader's position
  is read through the global that the game's GetLeader uses ([game+0x1051838] -> CBattleMainTask, no scan) and
  mapped by steamN_position; outside battle, or if the leader has no mapped controller, nothing is filtered.
  tt_find_main also uses that fast path before the scan.
- Self-test (fake object): leader passes, other controller zeroed (button and stick), probe's query not filtered.

### Run 31 (v0.31, leader_only_input=1) - notes/run31_v0.31
- Until the battle (~45 s) leader handle = 0 (no filter). In battle: Edge leader -> DualSense handle; after R3 on
  controller 1 -> handle 1 (Arumat); R3 on 2 -> Sarah; R3 on the DualSense -> Edge; controller 1 -> Arumat.
- ~6300 queries neutralized per second = 2/3 of the total (the two controllers that are not the leader's). No freeze.
- **User confirmed: it worked** (only the leader's controller moves the leader; R3 switches; menus ok).

## Release: so4coop tag-team 1.1
- Same as 1.0 + hook_steam=1 and leader_only_input=1 on in the template so4coop.ini: in battle only the leader's
  controller commands the leader. README/LEIA-ME updated. Package build/release/so4coop-tagteam-1.1.zip.
  Tested on local Wine (version, ini, filter self-tests, refusal outside battle).

## Phase 5 - real co-op (research)
- Two paths: (1) the proper one: find where the AI decides the actions of a member that is not the leader and replace them
  with controller 2's input (long reverse engineering); (2) a quick experiment with proven pieces:
  alternate the leader between two characters every N ms and, on each switch, let through only that
  character's controller.
- v0.32 (src/coopslice.c): F2 toggles; coop_a/coop_b (positions, default 0 and 1), coop_slice_ms (default 50).
  Each slice: validates the battle, writes the leader's position (same checks as tag-team) and sets the filter to
  that position's controller (steamN_position). Turning it off returns the leader to coop_a. Needs
  leader_only_input=1 to separate the input. Local test: refusal outside battle. In the release the code stays
  inert (F2 does not exist there).
- Expected problems: camera going back and forth, switch animation, AI taking over the other half of the time.

### Run 32 (v0.32) - notes/run32_v0.32 — co-op slices
- F2 turned on, turned off after 16 slices (~1 s, double tap?) and turned on again: ~20 s, 321 slices, 0 skipped; the
  filter kept up (leader handle alternating DualSense / controller 2). Turned off by itself at the end of the battle.
- Slices of ~60 ms (~16 switches/s), not 50 ms: the probe's loop sleeps 30 ms between passes.
- No freeze. User's report still needed (camera, control of each character, smoothness).
- **User's report (run 32):** steady camera; Edge and Sarah each followed their own controller; complete actions with
  their own skills; the AI never took over; "almost playable". Request: 4-player co-op.
- v0.33 (coopslice.c rewritten): participants = every connected Steam controller with steamN_position (up to 4);
  only **active** players get slices (stick > 0.25 or a button in the last coop_idle_ms = 1500 ms; a single active one
  = 100% of control; nobody active = leader stays as is); own thread with a ~1 ms step, slice
  coop_slice_ms = 40; coop_auto=1 turns on when the battle starts and pauses at the end (F2 still toggles).
  Activity measured on the probe's main thread (steamctl: digital actions + analog 1 and 2 through Steam,
  never filtered). Local test: ini read, refusal outside battle.

### Run 33 (v0.33) - notes/run33_v0.33 — co-op in 3 battles
- coop_auto turned on at the start of each battle and paused at the end (3 battles, without F2).
- Slices divided according to activity: moments with 1, 2 and **3 active players**; final total 1=608, 2=521,
  3=906 slices; ~20-25 switches/s; 2 of 2099 slices skipped (end of battle). The game ran 6 min without problems.
- User's report still needed on 2 and 3 players (camera, control, smoothness).
- **User's report (run 33): plays well.** 2 players: perfect. 3 players: only a visual glitch — the white
  cursors of each mapped character on the battle mini-map flicker, and the character driven by each controller keeps
  flashing white (leader marker redrawn on every switch). L2/R2 (skills) worked as in
  single-player. 1 player: no glitches.

## Release: so4coop co-op 1.2
- Battle co-op for up to 4 players (leader slices for active players), on in the template so4coop.ini
  (coop_auto=1, coop_slice_ms=40, coop_idle_ms=1500, coop_active_only=1, hook_steam=1, leader_only_input=1,
  steam1..4_position=0..3). README/LEIA-ME rewritten: how it works, known issues (leader marker and
  mini-map cursors flickering with 3+ players), installation, how to find each controller's number in the log,
  options. Package build/release/so4coop-coop-1.2.zip; tested on local Wine (version, ini, refusal outside
  battle, filter self-tests).

## Phase 6 - reducing the co-op flicker
- Release 1.2 installed in the game (2c2afc44…), user's so4coop.ini kept.
- vtables (offline RTTI): CThisIs game+0xa3cd98 (19 methods), CMinimapPlayerMarkerElement game+0xa3c288 (53),
  CMinimap game+0xa3c478, CTargetSign game+0xa3cb88, CArrowCursorManager game+0xa51910.
- **CThisIs = "this is your character" popup**: vf12 (game+0x60cd40) reads the frame time (float at
  game+0x105903c, which is why so many UI classes read that global) and runs a state machine at this+0x30
  (table at game+0x60cf60): 0 creates resources, 1 advances, 2 idle, 3 appears (position +0x44/+0x48, opacity
  +0x4c, calls "show" through the vtable +0x178), 4 animates, 5 holds (time at +0x40), 6 fades (opacity down to 0).
  Restarts at 3 on every leader switch -> in co-op (~25 switches/s) it flickers all the time.
- Who calls GetLeader in the UI: CMinimapPlayerMarkerElement vf41/vf42, CMinimapCharacterMarkerElement
  vf41/vf42, CSightInCursor vf32, CTargetBoard vf29/vf32 -> the mini-map cursors flicker because they draw the
  leader of the moment.
- Fix 1 (v0.34, coop_quiet_marker=1): find CThisIs once per battle (scan of the battle objects
  on the probe's main thread) and, after each slice, if it is in 3-5, set opacity 0 and state 6 (the game
  itself finishes the fade). Data only, without touching code.
- Fix 2 (proposed, ask for authorization): patch the game's GetLeader in memory so that the UI functions
  above see a stable "display leader" (first change to game code).
- Fix 2 (v0.35, src/displayfix.c, coop_display_fix=1 / coop_display_pos=): 8 UI calls to
  GetLeader redirected (only the rel32 target of the call instructions; GetLeader and gameplay untouched):
  0x6075e1/0x609372 CMinimapCharacterMarkerElement vf41/vf42, 0x6080bd/0x609e30 CMinimapPlayerMarkerElement
  vf41/vf42, 0x6161f1/0x616911 CSightInCursor vf32, 0x61b150 CEftSightOutLine vf12, 0x6593a8 helper used only
  by CTargetBoard vf29/vf32. Destination: gl_display() = same computation as GetLeader, but with co-op active returns the
  member at coop_display_pos (default: lowest mapped position). CBattleCamp (menu) keeps the real leader.
- Application: 14-byte jump near the game (rel32 range), page made writable BEFORE pausing
  threads, threads paused with SuspendThread (no ptrace), handles kept to resume without a new snapshot
  (a snapshot uses the heap; a paused thread may hold the heap lock), each site verified (E8 -> GetLeader)
  before and during the pause or nothing changes, instruction cache flushed, protection restored. Once per
  session, when co-op turns on.
- Self-test (call inside the probe itself): before 4, after 2002 -> works.
- Round 35 (v0.35, 3 players): fix 2 applied without freezing (60 threads paused, 8 sites), mini-map
  became steady. The CThisIs popup was never caught in 3-5 in ~2250 switches -> it is not what flickers (fix 1 had no
  effect). NEW PROBLEM reported: with each player moving their character (different directions, or all
  upward) the characters get stuck/stop and "seem controlled by the same one"; in round 33 (v0.33) this was not
  noticed. Two hypotheses: (a) some patched site is not purely visual (CSightInCursor vf32 calls methods of the
  character: vf+0x530, 0x53d130; CTargetBoard's helper only measures distance to the target, looks visual);
  (b) 40 ms slices with 3 players (40 of every 120 ms; the rest of the time the AI moves the character).
- v0.36 to separate them: F5 toggles live ALL 8 sites -> MINI-MAP ONLY -> OFF (= v0.34) (the sites
  stay patched; the cleared bit returns the real leader); gl_display identifies the site by the return
  address and counts calls per site ([display] report every 10 s); F6 cycles the slice 40 -> 80 -> 150 ms.
  Optional setting coop_display_sites= (hex, default ff).
- Round 36 (v0.36, F5/F6 live; log in notes/run36_probe.log): battle 1 = mini-map only + 80 ms (2 players),
  2-3 = fix off + 80/40 ms, 4 = off + 150 ms (3 players). The user: NORMAL movement in
  all of them; mini-map steady with "mini-map only"; the white flashing on the characters was gone. In round 35 (8 sites)
  the symptom was "characters pulled back" -> the sight cursor (CSightInCursor) and/or CTargetBoard's
  helper feed gameplay and need the real leader. The sight outline (CEftSightOutLine) was never
  called. Calls per frame (~60/s): mini-map vf42 (both), sight cursor (both), target; the mini-map's vf41
  almost never.
- v0.37: only the 4 mini-map sites are patched; F5 turns the mini-map fix on/off; F6 remains
  (slice 40/80/150 ms); popup (coop_quiet_marker) off by default.
- Round 37 (v0.37, 40 ms; log in notes/run37_probe.log): with only the mini-map patched (948 of 948 calls
  returned Edge) the mini-map STILL flickered and the white on the characters came back. In round 35 (8 sites, 40 ms)
  the mini-map was steady; in round 36 battle 1 (mini-map, 80 ms) too -> the white cursors come from the
  sight cursor and/or the target board, and the longer slice (fewer switches/s) masks the flicker.
  Controller 1 went unused until R3 (no button/stick before that). User prefers a default slice of 40 ms.
- v0.38: all 8 sites patched again; F5 cycles MODE 1 MAP -> 2 MAP + TARGET BOARD -> 3 MAP +
  SIGHT CURSOR -> 4 OFF, to find which of them calms the visuals without pulling the characters.
- Round 38 (v0.38, 40 ms, 3 players; log in notes/run38_probe.log), user's answers:
  MODE 2 (map + target board): mini-map flickers, normal movement, **white on the characters gone**;
  MODE 3 (map + sight cursor): **mini-map steady**, normal movement, white continues.
  -> the sight cursor (CSightInCursor vf32) draws the white cursors on the mini-map; the target board
  (CTargetBoard's helper) causes the white on the characters. Neither of them alone pulls the characters.
- v0.39: turns both on together (mask 0xbf) by default; F5 = on/off. Tests whether the combination (as
  in round 35) is what pulled the characters back.
- Round 39 (v0.39, map + target board + sight cursor, 40 ms, 3 players, 3 battles): normal
  movement, steady mini-map, no white on the characters. Round 35's "pulled back" did not return (cause
  not confirmed; if it returns, coop_display_sites= can turn parts off).
- Release 1.3: display fix on (mask bf), coop_display_fix/coop_display_pos/coop_display_sites
  documented in README/LEIA-ME and in so4coop.ini; package build/release/so4coop-coop-1.3.zip.

## Phase 7 - PlayStation button icons (without Special K)
- Game data: 0000.bin (12.9 GB, encrypted) and 0001.bin (36.7 GB, "KCAP" packages with tri-Ace's SLZ
  compression, no readable names). Replacing textures on disk = reverse engineering both + rewriting 36 GB.
- The exe does not import d3d11/dxgi/xinput (it loads them through LoadLibrary/GetProcAddress in the protected code); the
  strings "PlayStation 4" etc. are a list of audio platforms, not an icon option. No Special K
  pack on disk.
- Chosen path: replacement at run time inside our xinput1_3.dll, as Special K does: the game
  draws with D3D11 (DXVK on Proton) and all devices of the same implementation share the vtable; a
  throwaway device of ours gives us the vtable and CreateTexture2D (slot 5) is redirected.
- v0.40 (src/texhook.c, research version only): at 45 s it installs the hook; every texture created with data
  goes to so4coop_textures/textures.txt (FNV-1a 64 hash of level 0, size, DXGI format, flags) and each
  new texture in a UI format (RGBA/BGRA 8 bits, BC1/2/3/7) is saved once as .dds (up to 6000
  files / 3 GB). Observes only. Self-test: hook + 64x64 texture through a 2nd device -> saved, Pillow reads it.
- Next: find the sheet with the Xbox icons (A/B/X/Y colors), draw the PlayStation icons and replace them.
- Round 40 (v0.40; list in notes/run40_textures.txt): the hook worked (553 textures with data, 762 without),
  but 0 .dds saved: the game creates them in TYPELESS formats (70 BC1, 73 BC2, 76 BC3, 27 RGBA8, 97 BC7, 83 BC5) and
  v0.40 only saved the typed ones. All immutable (usage 1). Candidates for UI sheets: 4x 8192x4096 and
  2x 2048x4096 in BC3 (DXT5). Nothing created between 45 s and ~60 s (title screen before the hook).
- v0.41: saves the TYPELESS ones (file marked as UNORM) and also lists the textures created without data.
  Self-test with RGBA8 and BC3 TYPELESS: both saved and read by Pillow.
- Round 41 (v0.41): 513 .dds saved (394 MB). The user has the pack "PlayStation Button Prompts.zip" (2017,
  for Special K): SK's CEGUI + one texture, SK_Res/inject/textures/61a1af48.dds = 4096x4096 BC7 (98), 1 mip:
  the main UI sheet (L1/R1/L2/R2, OPTIONS, keyboard keys, portraits). It was NOT among the
  saved ones (loaded on the title screen, before the 45 s hook). Another 4096x4096 BC7 sheet of the game (defd...)
  already has ○ ✕ △ □ and OPTIONS next to A/B/X/Y (leftovers from the PS4 version).
- v0.42: compatible with Special K packs without Special K: name = CRC-32C of level 0 (like SK's
  crc32_tex: block rows, height/4 + height%4 in the fast path), SSE4.2 instruction; if the game creates a texture
  whose CRC has a file in SK_Res\inject\textures\<crc>.dds, the texture is created from the file (game's format
  kept when the block is the same; mips from the file). Hook installed on DLL load (thread that waits for
  d3d11.dll; only kernel32 + d3d11), tex_hook=0 turns it off. textures.txt gained the special_k_name column.
- Self-test (system Wine): CRC-32C("123456789") = e3069283; 64x64 RGBA and 4096x4096 BC7 TYPELESS (2 mips) with
  the pack's REAL file renamed to its CRC -> INJECTED (4096x4096 fmt 97 mips 1). CRCs equal to those of
  an independent implementation in Python.
- Round 42 (v0.42; log in notes/run42_probe.log): the game FROZE before the title screen. Hook installed at
  5.8 s (our device created ~270 ms after the game loaded d3d11.dll), and then 0 textures, 0 Steam
  calls in 80 s (in round 41 there were ~600 RunFrame/s already at 10 s) -> the main loop never started. Cause: creating
  a D3D11 device of ours while the game initializes its own.
- v0.43: no device of ours at startup. The thread loads d3d11.dll (only loads it) and puts a 14-byte jump
  (jmp [rip]) at the start of D3D11CreateDevice and D3D11CreateDeviceAndSwapChain (follows E9/EB/FF25 thunks; in Proton
  10's DXVK both start with a normal prologue, 0x50 bytes apart). The game's 1st call restores the original
  bytes, runs the real function and hooks the vtable of the returned device, before any texture. If nothing
  is caught in 60 s: it disarms and uses v0.41's way (own device, once the game's already exists).
- Self-test (system Wine): creation caught through the armed export (hook at 131 ms), 2 injections (64x64 RGBA and
  4096x4096 BC7 2 mips) -> works.
- Round 43 (v0.43, in the TEST COPY /mnt/STEAM/so4coop-test via launch.sh, together with Lyall's SO4Fix
  (winmm.dll + SO4Fix.asi); log in notes/run43_probe.log): **worked**. Hook armed at 4 ms, the game's own
  device caught at 2766 ms (D3D11CreateDeviceAndSwapChain), 61a1af48.dds INJECTED (game 4096x4096 BC7 2 mips
  -> file 1 mip), 0 failures; normal title screen (round 42's freeze is gone). User confirmed the PlayStation
  icons on screen. Co-op in the same session: 1 battle, 364 slices of 40 ms between controllers 1 and 2, display
  fix 5852 of 5852 steady. so4coop and SO4Fix coexist (xinput1_3 + winmm, WINEDLLOVERRIDES with ";").
- Next: take the texture hook to the release version (injection only; without saving .dds or textures.txt,
  which took up 606 MB) -> release 1.4.
- Release 1.4 (code): texhook.c goes into the release version with replacement only (no textures.txt, no .dds, no FNV
  hash; the CRC is only computed when there is a pack) and with no hook at all if SK_Res\inject\textures has no .dds;
  tex_hook=1 in so4coop.ini. Self-test (system Wine, absolute DLL path + WINEDLLOVERRIDES, otherwise
  rundll32 loads the builtin xinput1_3): with the 2 test files -> 2 injections, 0 saved, no
  so4coop_textures folder; without a pack -> "button prompts: off", nothing armed. Research version: self-test still WORKS.
- Round 44 (release 1.4 in the copy; log in notes/run44_so4coop.log): PS icons and co-op worked, but the
  user saw a **double skill press**: the GameSir's L2/LT (controller 1, Arumat) fired Dragon Roar and Sarah
  (controller 2) fired Solar Flare at the same time. Observed in the log: the two controllers are distinct devices (most
  presses appear on only one; the GameSir does not appear twice). No events for actions 13/14 in this session,
  so the log cannot show which action is the GameSir's L2 (unknown).
  Deduced (not confirmed): (1) cs_thread wrote the new leader BEFORE switching sh_leader_h, so for an instant the
  previous player's controller reached the new leader; (2) the game reads the controller and acts on the next frame for whoever
  is the leader at that moment; a switch in between delivers the press to the next player.
- v0.44 / 1.4.1: (a) during the switch sh_leader_h = SH_NOBODY (no controller gets through) and only then the new leader's;
  (b) sh_dig marks the time of each new press on the leader's controller (a held button does not count; new leader = state
  zeroed, because to the game it is a new press); the turn does not change until coop_press_guard_ms (50) after a press,
  with at most +120 ms of turn. The [co-op] report gained "held for a press". Self-test (so4coop_hooktest, research
  and release): press seen, held is not a new press, during the switch leader 0 and other 0 -> WORKS.
- Round 45 (1.4.1 in the copy; log in notes/run45_so4coop.log): **no double press** (user). Controller 1 (GameSir,
  Arumat) pressed actions 13/14 (L2/R2, probably) 33 times; no paired presses between the controllers; the guard
  held the switch 127 of 159 times (average turn ~63 ms instead of 40 with both players active); the user did not
  report any slowness.

## 2026-11-21 (night): SO4Fix never loaded; 4 players with a keyboard
- Finding: with the game open (real install), the mapped winmm.dll was Proton's builtin, not Lyall's Ultimate ASI
  Loader; no SO4Fix.log anywhere (not even in the test copy). The exe does not import winmm (it imports advapi32,
  gdi32, kernel32, msvcp110, msvcr110, ole32, shell32, shlwapi, steam_api64, user32, wininet); what pulls in winmm are
  Wine's builtin DLLs -> the builtin one comes in. Round 43's note ("so4coop and SO4Fix coexist") was wrong: only
  so4coop was running. Symptom: Steam screenshot 1914x1048 (window with borders, black bars), bad alt+tab.
- Fix: winmm.dll -> wininet.dll (a name accepted by Ultimate ASI Loader, imported directly by the exe) in the real one and in the
  copy; WINEDLLOVERRIDES="xinput1_3=n,b;wininet=n,b". The copy's launch.sh updated. To be confirmed: SO4Fix.log appears.
- 4th player without a 4th controller: tools/kbpad.py (python-evdev) creates a virtual Xbox 360 controller (uinput, 045e:028e)
  driven by the keyboard, which stays grabbed (grab); keys with no function go through a virtual keyboard; Scroll Lock/Pause
  turns it on/off. Steam sees it as one more controller -> steam4_position=3, without changing the mod. tools/so4play.sh (launch
  option) starts kbpad 1 s before the game and stops it on exit; log in tools/so4play.log. Reading the keyboard
  needs the udev rule tools/70-so4kbpad.rules (uaccess only for the Telink keyboard 248a:16ab).
- The real install's so4coop.ini was the old one from 11/17; it is now 1.4.1's with steam1=2, steam2=1, steam3=0, steam4=3
  (old one in real-install-backup-20261121/so4coop.ini.before-4p).
- Round 46 (real install, 1.4.1 + SO4Fix + kbpad): SO4Fix crashed the game ~3 s after launch (Proton log:
  page fault in msvcp140+0x18c34 called from SO4Fix.asi+0x165a0). Prefix 609150 has msvcp140 14.0.24215;
  SO4Fix (VS 2022 17.10+) needs 14.40+ (std::mutex change). Fix: msvcp140/vcruntime140/vcruntime140_1
  14.44 (from app 595520's prefix) in the game folder (real and copy) + n,b overrides in so4play.sh. SO4Fix.log:
  v0.9.0 loaded, all fixes applied (1920x1080 borderless, intro, HUD, FOV, shadows).
  Co-op: Steam listed 3 controllers and the keyboard's virtual one came as controller 1 (not 4): handles 000D9089..(kbpad),
  00011C66..6D5EC4E5 (GameSir), 00245E28..E869A2DE (DualSense). The keyboard went to Arumat (steam1=2) and Myuria
  (steam4) stood still. Steam does not number by connection order.
- v0.45 / 1.4.2: xbox_position=P. Every 1 s, until all controllers are known, SteamAPI_ISteamController_
  GetDigitalActionOrigins(handle, current action set, actions 1..16) -> origin 114-191 (Xbox One/360) = Xbox, other = other.
  Once all are known: the 1st Xbox gets P, the others get steam1.., steam2.. in Steam's order (sc_remap;
  configured in sc_cfg, in effect in sc_pos). New handle at an index -> reclassify. Without xbox_position nothing changes.
  Self-test so4coop_maptest (release and research): types by origin OK, 5 cases (including round 46's) -> WORKS;
  hooktest still WORKS. In the copy: 1.4.2 + ini steam1=1 (GameSir), steam2=0 (DualSense), steam3=2, xbox=3;
  launch.sh now goes through tools/so4play.sh (kbpad + SO4Fix). To be confirmed: real origin values in the log.
- Round 47 (1.4.2 in the copy; log in notes/run47_so4coop.log): WRONG detection. Origins: controller 1 (handle
  ..5F4DC78F, exists only since kbpad = keyboard) 246; controller 2 (..6D5EC4E5, GameSir) 40; controller 3 (..E869A2DE,
  DualSense) 120 -> the DualSense was classified as Xbox and went to Myuria, the keyboard to Sarah. The enum's
  ranges (PS4 50-113, Xbox 114-191) do not hold for what this Steam returns through interface 005.
- v0.46 (1.4.2 again): type by the NAME of the origin's icon (GetGlyphForActionOrigin, fallback GetStringForActionOrigin):
  contains "xbox" -> Xbox, another name -> other, empty -> unknown (the ini numbering stays). Log: "controller kind"
  with action, origin, icon and name, and each controller's action:origin list. Self-test maptest + hooktest -> WORKS.
- v0.47 / 1.5: join mode (coop_join=1) for Remote Play Together and Sunshine/big-remote-play (BigLinux; Sunshine +
  Moonlight, up to 4 guest devices, Sunshine's virtual controllers via libvirtualhid, DualSense chosen by the
  client's type). Guests arrive as more Steam controllers, in varying order, and may be Xbox -> neither
  steamN nor xbox_position works. Now: the 1st press in a battle takes the 1st free position, R3 (take_action) moves
  to the next free one; ownership kept by handle (sc_jh/sc_jp, 16) for the whole session; a disconnected controller's position
  becomes free. SC_MAXCTL 4 -> 8 (steam5..8_position, co-op report with 8). displayfix: no positions -> 0.
  Self-test so4coop_jointest (5 controllers, leaving, changed order, full party) WORKS; maptest/hooktest WORKS.
  In the copy: 1.5 + coop_join=1. Linux side: keyd now takes only the physical keyboard (ids 248a:16ab) — with '*' it
  also grabbed the Sunshine guests' virtual keyboard and kbpad's passthrough. A guest's keyboard reaches the
  game as a plain keyboard (outside the filter) -> guests must use a controller.
- Round 48 (1.5 in the copy, coop_join=1; log in notes/run48_so4coop.log): **worked**. 3 controllers; each one joined
  on its 1st press, in the order of the presses: controller 3 (DualSense) -> Edge, controller 1 (keyboard/kbpad) -> Sarah, controller 2
  (GameSir) -> Arumat. All three got turns (294 / 93 / 223 slices). R3 to switch was not used. No remote
  guests yet (Remote Play Together / big-remote-play to be tested).
