/*
 * so4coop - co-op experiment: time-sliced leader (v0.32, research).  Included after steamhook.c.
 *
 * The game controls one character (the leader); the others are AI. Run 31 proved that the leader
 * byte switches control completely and that the Steam hook can give the game only one controller's
 * input. This experiment alternates both, every coop_slice_ms (default 50 ms): leader = coop_a's
 * position with only coop_a's controller, then coop_b's position with only coop_b's controller.
 * Controllers come from steamN_position in so4coop.ini (the controller whose position is coop_a /
 * coop_b). F2 toggles it during a battle; turning it off gives the leader back to coop_a.
 * Expected problems: the camera chasing the alternating leader, switch animations, AI taking over
 * in the other half. Uses only proven pieces: the validated leader write and the input filter.
 */

static uint32_t cs_a = 0, cs_b = 1, cs_ms = 50;
static int cs_loaded, cs_on, cs_phase;
static uint64_t cs_last, cs_t0, cs_report;
static volatile long cs_slices, cs_skips;

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
        if (tt_find_value(buf, r, "coop_a=", &v)) cs_a = v;
        if (tt_find_value(buf, r, "coop_b=", &v)) cs_b = v;
        if (sc_find_dec(buf, r, "coop_slice_ms=", &v) && v >= 16 && v <= 2000) cs_ms = v;
    }
    wsprintfA(b, "  co-op slice: F2 toggles; positions %lu and %lu, %lu ms each\r\n", (unsigned long)cs_a, (unsigned long)cs_b, (unsigned long)cs_ms);
    logs(b);
}

/* controller handle whose steamN_position is pos (0 = none) */
static uint64_t cs_handle_for(uint32_t pos)
{
    for (int k = 0; k < sc_nctl && k < SC_MAXCTL; k++) if (sc_pos[k] == pos) return sc_handles[k];
    return 0;
}

/* validated leader write (same checks as tt_take_pos, no log): 1 = written */
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

static void coop_toggle(void)
{
    char b[160];
    if (!cs_loaded) cs_load();
    if (cs_on) {
        cs_on = 0;
        cs_write(cs_a);
        wsprintfA(b, "=== CO-OP SLICE OFF after %ld slices (%ld skipped); leader back to position %lu ===\r\n", cs_slices, cs_skips, (unsigned long)cs_a);
        logs(b);
        return;
    }
    uint32_t pos;
    if (!tt_leader_pos(&pos)) { logs("=== CO-OP SLICE: no battle ===\r\n"); return; }
    cs_on = 1; cs_phase = 0; cs_slices = cs_skips = 0;
    cs_t0 = cs_last = cs_report = GetTickCount64();
    wsprintfA(b, "=== CO-OP SLICE ON: position %lu (controller %p) <-> position %lu (controller %p), %lu ms ===\r\n",
              (unsigned long)cs_a, (void *)(uintptr_t)cs_handle_for(cs_a), (unsigned long)cs_b, (void *)(uintptr_t)cs_handle_for(cs_b), (unsigned long)cs_ms);
    logs(b);
}

/* hotkey thread, every loop (30 ms Sleep; slices are timed by cs_ms) */
static void coop_slice_poll(void)
{
    char b[160];
    if (!cs_on) return;
    uint64_t now = GetTickCount64();
    if (now - cs_last < cs_ms) return;
    cs_last = now;
    cs_phase ^= 1;
    uint32_t pos = cs_phase ? cs_b : cs_a;
    if (!cs_write(pos)) {
        __sync_fetch_and_add(&cs_skips, 1);
        uint32_t p;
        if (!tt_leader_pos(&p)) { cs_on = 0; logs("=== CO-OP SLICE OFF: battle ended ===\r\n"); }
        return;
    }
    sh_leader_h = cs_handle_for(pos);                  /* only this position's controller reaches the game */
    __sync_fetch_and_add(&cs_slices, 1);
    if (now - cs_report >= 10000) {
        cs_report = now;
        wsprintfA(b, "  [co-op slice t=%lus] %ld slices, %ld skipped\r\n", (unsigned long)((now - cs_t0) / 1000), cs_slices, cs_skips);
        logs(b);
    }
}

/* test hook (not used by the game): toggle the experiment; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_cooptest(void) { coop_toggle(); }
