/*
 * so4coop - co-op: time-sliced leader for up to 4 players (v0.33).  Included after steamhook.c.
 *
 * The game controls one character (the leader); the others are AI. Run 32 (v0.32, 2 players,
 * ~60 ms slices): camera steady, Edge and Sarah each followed their own controller, actions
 * completed with their own skills, the AI never took over -> "almost playable".
 * v0.33:
 *   - participants = every connected Steam controller with a steamN_position (up to 4 players);
 *   - only ACTIVE players get slices (stick beyond 0.25 or any button within coop_idle_ms, default
 *     1500 ms); one active player gets 100% control; nobody active -> leader left as it is;
 *   - own thread with ~1 ms timing, slice coop_slice_ms (default 40);
 *   - coop_auto=1: starts when a battle starts and pauses when it ends (F2 still toggles).
 * Each slice: validated leader write (same checks as tag-team) + input filter on that player's
 * controller (needs hook_steam=1 and leader_only_input=1).
 */

static uint32_t cs_ms = 40, cs_idle = 1500, cs_auto, cs_active_only = 1;
static int cs_loaded, cs_thread_started;
static DWORD cs_tid;
static volatile int cs_on;
static uint64_t cs_t0, cs_report;
static volatile long cs_slices, cs_skips, cs_per[SC_MAXCTL];
/* v0.34: the "this is your character" popup (CThisIs) restarts at every leader change: state 3 =
 * pop up, 4 = animate, 5 = hold, 6 = fade out (+0x4c opacity -> 0, then hidden), 2 = idle.
 * With coop_quiet_marker=1 the popup is sent straight to a finished fade-out after each slice. */
static uint32_t cs_quiet = 1;
static volatile uintptr_t cs_thisis;
static volatile int cs_need_find = 1;
static volatile long cs_quieted;

static void cs_quiet_marker(void)
{
    uintptr_t o = cs_thisis;
    if (!cs_quiet || !o || !mem_readable(o + 0x30, 0x20)) return;
    const char *cn = class_of(o);
    if (!cn || !str_eq(cn, "CThisIs@Battle@@")) { cs_thisis = 0; cs_need_find = 1; return; }
    int st = *(volatile int *)(o + 0x30);
    if (st >= 3 && st <= 5) {
        *(volatile float *)(o + 0x4c) = 0.0f;
        *(volatile int *)(o + 0x30) = 6;
        __sync_fetch_and_add(&cs_quieted, 1);
    }
}

static void cs_load(void)
{
    char b[200];
    static char buf[4096];
    uint32_t v;
    cs_loaded = 1;
    uint16_t path[520];
    DWORD n = GetModuleFileNameW(self_module, path, 512);
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    path[n] = 0;
    wcat(path, "so4coop.ini");
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != (HANDLE)-1) {
        DWORD r = 0;
        ReadFile(h, buf, sizeof buf - 1, &r, 0);
        CloseHandle(h);
        if (sc_find_dec(buf, r, "coop_slice_ms=", &v) && v >= 10 && v <= 2000) cs_ms = v;
        if (sc_find_dec(buf, r, "coop_idle_ms=", &v) && v >= 100 && v <= 60000) cs_idle = v;
        if (tt_find_value(buf, r, "coop_auto=", &v)) cs_auto = v ? 1 : 0;
        if (tt_find_value(buf, r, "coop_active_only=", &v)) cs_active_only = v ? 1 : 0;
        if (tt_find_value(buf, r, "coop_quiet_marker=", &v)) cs_quiet = v ? 1 : 0;
    }
    wsprintfA(b, "  co-op: auto %lu (F2 toggles in the research build), slice %lu ms, active players only %lu (idle after %lu ms), quiet marker %lu\r\n", (unsigned long)cs_auto,
              (unsigned long)cs_ms, (unsigned long)cs_active_only, (unsigned long)cs_idle, (unsigned long)cs_quiet);
    logs(b);
}

/* validated leader write (same checks as tt_take_pos, no log): 1 = written or already set */
static int cs_write(uint32_t target)
{
    uintptr_t mt = tt_main_fast();
    if (!mt || !mem_readable(mt + 0x58, 0x18)) return 0;
    uintptr_t party = *(const uintptr_t *)(mt + 0x58), base = *(const uintptr_t *)(mt + 0x60), blk = *(const uintptr_t *)(mt + 0x68);
    if (!(blk >= base && blk - base < 0x200) || !mem_readable(blk, 8)) return 0;
    uint32_t mx = *(const uint32_t *)blk, cur = *(const uint32_t *)(blk + 4);
    const char *pc = class_of(party);
    if (mx < 1 || mx > 3 || cur > mx || target > mx || !pc || !str_eq(pc, "CChrManager@Battle@@")) return 0;
    if (!mem_readable(party + 0x5a0 + 8 * target, 8) || !party_name(*(const uintptr_t *)(party + 0x5a0 + 8 * target))) return 0;
    if (cur != target) *(volatile BYTE *)(blk + 4) = (BYTE)target;
    return 1;
}

static DWORD WINAPI cs_thread(LPVOID u)
{
    char b[300];
    (void)u;
    int turn = 0;
    uint64_t last = 0;
    for (;;) {
        Sleep(1);
        if (!cs_on) {
            if (cs_auto) {                                 /* auto: start with the battle */
                uint32_t p;
                if (tt_leader_pos(&p)) { cs_on = 1; cs_t0 = cs_report = GetTickCount64(); logs("=== CO-OP ON (battle started) ===\r\n"); }
                else Sleep(200);
            } else Sleep(50);
            continue;
        }
        uint64_t now = GetTickCount64();
        if (now - last < cs_ms) continue;
        last = now;
        /* participants: connected controllers with a position, active recently (or all) */
        int idx[SC_MAXCTL], np = 0;
        for (int k = 0; k < sc_nctl && k < SC_MAXCTL; k++)
            if (sc_pos[k] != 0xffffffff && (!cs_active_only || now - sc_active_t[k] < cs_idle)) idx[np++] = k;
        if (!np) continue;                                 /* nobody active: leave the leader as it is */
        turn = (turn + 1) % np;
        int k = idx[turn];
        if (!cs_write(sc_pos[k])) {
            __sync_fetch_and_add(&cs_skips, 1);
            uint32_t p;
            if (!tt_leader_pos(&p)) {                      /* battle over */
                cs_on = 0;
                cs_thisis = 0; cs_need_find = 1;
                wsprintfA(b, "=== CO-OP %s: battle ended (%ld slices) ===\r\n", cs_auto ? "paused" : "OFF", cs_slices);
                logs(b);
            }
            continue;
        }
        sh_leader_h = sc_handles[k];                       /* only this player's controller reaches the game */
        cs_quiet_marker();
        __sync_fetch_and_add(&cs_slices, 1);
        __sync_fetch_and_add(&cs_per[k], 1);
        if (now - cs_report >= 10000) {
            cs_report = now;
            wsprintfA(b, "  [co-op t=%lus] %ld slices (%ld skipped); per Steam controller: 1=%ld 2=%ld 3=%ld 4=%ld; active now: %d; popup quieted %ld\r\n",
                      (unsigned long)((now - cs_t0) / 1000), cs_slices, cs_skips, cs_per[0], cs_per[1], cs_per[2], cs_per[3], np, cs_quieted);
            logs(b);
        }
    }
    return 0;
}

static void cs_start_thread(void)
{
    if (cs_thread_started) return;
    cs_thread_started = 1;
    CreateThread(0, 0, cs_thread, 0, 0, &cs_tid);
}

static void coop_toggle(void)
{
    char b[200];
    if (!cs_loaded) cs_load();
    cs_start_thread();
    if (cs_on) {
        cs_on = 0;
        Sleep(5);
        wsprintfA(b, "=== CO-OP OFF after %ld slices (%ld skipped) ===\r\n", cs_slices, cs_skips);
        logs(b);
        return;
    }
    uint32_t pos;
    if (!tt_leader_pos(&pos)) { logs("=== CO-OP: no battle ===\r\n"); return; }
    cs_t0 = cs_report = GetTickCount64();
    cs_on = 1;
    logs("=== CO-OP ON ===\r\n");
}

/* hotkey thread: load the settings once; start the scheduler thread when coop_auto=1 */
static void coop_slice_poll(void)
{
    char b[160];
    if (!cs_loaded && sc_tried) { cs_load(); if (cs_auto) cs_start_thread(); }
    if (cs_on && cs_quiet && cs_need_find) {            /* once per battle, on this (main probe) thread */
        cs_need_find = 0;
        battle_pass();
        cs_thisis = battle_find("CThisIs@Battle@@");
        wsprintfA(b, "  co-op: popup (CThisIs) %p\r\n", (void *)cs_thisis);
        logs(b);
    }
}

/* test hook (not used by the game): toggle co-op; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_cooptest(void) { coop_toggle(); }
