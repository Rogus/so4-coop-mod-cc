/*
 * so4coop - co-op flicker fix 2: steady "display leader" for the mini-map (v0.35; v0.36 F5 modes + per-site counts; v0.37 mini-map sites only; v0.38 F5 = map / +target board / +sight cursor / off; v0.39 F5 = all on / off).  Included after coopslice.c.
 *
 * The mini-map markers, sight cursor, sight outline effect and target board ask GetLeader
 * (game+0x5000d0) for the leader every frame; during co-op the leader alternates ~25x/s, so they
 * flicker. Only their own call sites (E8 rel32 -> GetLeader) are redirected; GetLeader itself and
 * all gameplay code stay untouched:
 *   game+0x6075e1 CMinimapCharacterMarkerElement::vf41   game+0x609372 ...::vf42
 *   game+0x6080bd CMinimapPlayerMarkerElement::vf41      game+0x609e30 ...::vf42
 *   game+0x6161f1, game+0x616911 CSightInCursor::vf32    game+0x61b150 CEftSightOutLine::vf12
 *   game+0x6593a8 helper used only by CTargetBoard::vf29/vf32
 * They are redirected to gl_display(), which computes the leader exactly like GetLeader
 * (party->members[block[4]]) but, while co-op runs, returns the member at coop_display_pos.
 * Patch: a 14-byte jump near the game (rel32 reach), page made writable BEFORE pausing threads (no
 * Wine lock held while they are paused), game threads paused with SuspendThread (not ptrace), every
 * site verified (E8 -> GetLeader) or nothing is changed, 8 rel32 written, instruction cache flushed,
 * threads resumed, protection restored. Applied once per game session, when co-op first starts.
 */

__declspec(dllimport) void *WINAPI VirtualAlloc(void *, uint64_t, DWORD, DWORD);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE, const void *, uint64_t);

#define GL_RVA 0x5000d0
/* v0.37 patched only the 4 mini-map sites: round 37 (40 ms) the mini-map still flickered, while round 35 (all 8
 * sites, 40 ms) it was steady but movement broke ("characters pulled back"). So the white map cursors come
 * from the sight cursor or the target board, and one of them feeds gameplay. v0.38 patches the 8 sites again
 * and F5 cycles which ones get the steady leader:
 *   MAP (0-3) -> MAP + TARGET BOARD (+7) -> MAP + SIGHT CURSOR (+4,5) -> OFF -> MAP ...
 * The sight outline (6) was never called; it is patched but never enabled. */
static const uint32_t gl_sites[] = { 0x6075e1, 0x6080bd, 0x609372, 0x609e30, 0x6161f1, 0x616911, 0x61b150, 0x6593a8 };
#define GL_NSITES (sizeof(gl_sites) / sizeof(gl_sites[0]))
static uint32_t gl_fix = 1, gl_pos = 0xffffffff;
static volatile int gl_done, gl_applied;
static volatile long gl_calls, gl_shown, gl_cnt[GL_NSITES];
/* which sites get the steady leader; patched sites whose bit is clear just return the real leader */
#define GL_MAP 0x0f
#define GL_BOARD 0x80
#define GL_SIGHT 0x30
#define GL_BOTH (GL_MAP | GL_BOARD | GL_SIGHT)
static volatile uint32_t gl_mask = GL_BOTH;      /* v0.39: everything that helped in round 38 */
static uint64_t gl_report_t;

static const char *gl_mode_name(uint32_t m)
{
    return m == GL_BOTH ? "ON: MAP + TARGET BOARD + SIGHT CURSOR" : m == 0 ? "OFF (real leader)" : m == GL_MAP ? "MAP only"
         : m == (GL_MAP | GL_BOARD) ? "MAP + TARGET BOARD" : m == (GL_MAP | GL_SIGHT) ? "MAP + SIGHT CURSOR" : "custom";
}

/* same result as GetLeader, except a steady member while co-op runs (for the enabled sites) */
static __attribute__((noinline)) uintptr_t gl_display(uintptr_t sys)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0) - 5 - game_base;   /* the call site that got here */
    uint32_t bit = 0;
    for (uint32_t i = 0; i < GL_NSITES; i++)
        if (ra == gl_sites[i]) { __sync_fetch_and_add(&gl_cnt[i], 1); bit = 1u << i; break; }
    uintptr_t main = *(const uintptr_t *)sys;
    uintptr_t blk = *(const uintptr_t *)(main + 0x68);
    uint32_t pos = *(const uint8_t *)(blk + 4);
    uintptr_t party = *(const uintptr_t *)(main + 0x58);
    __sync_fetch_and_add(&gl_calls, 1);
    if (cs_on && (gl_mask & bit) && gl_pos <= 3 && gl_pos <= *(const uint32_t *)blk) {
        uintptr_t m = *(const uintptr_t *)(party + 0x5a0 + 8 * (uintptr_t)gl_pos);
        if (m) { __sync_fetch_and_add(&gl_shown, 1); return m; }
    }
    return *(const uintptr_t *)(party + 0x5a0 + 8 * (uintptr_t)pos);
}

/* a 14-byte absolute jump within +-2 GB of 'near' */
static uintptr_t gl_trampoline(uintptr_t nearaddr, void *target)
{
    for (uintptr_t d = 0x1000000; d < 0x70000000; d += 0x100000)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            uintptr_t want = (nearaddr + (intptr_t)sgn * (intptr_t)d) & ~(uintptr_t)0xffff;
            BYTE *t = (BYTE *)VirtualAlloc((void *)want, 0x1000, 0x3000 /* reserve|commit */, 0x40 /* rwx */);
            if (!t) continue;
            intptr_t dist = (intptr_t)t - (intptr_t)nearaddr;
            if (dist > 0x7ff00000 || dist < -0x7ff00000) continue;
            t[0] = 0xff; t[1] = 0x25; t[2] = t[3] = t[4] = t[5] = 0;            /* jmp [rip+0] */
            *(uint64_t *)(t + 6) = (uint64_t)(uintptr_t)target;
            return (uintptr_t)t;
        }
    return 0;
}

/* pause every thread of the process except the calling one (and skip2); the handles are kept so
 * that resuming needs no snapshot (a snapshot allocates heap memory, and a paused thread may hold
 * the heap lock) */
#define GL_MAXTH 512
static HANDLE gl_th[GL_MAXTH];
static uint32_t gl_nth;
static uint32_t gl_pause(DWORD skip2)
{
    gl_nth = 0;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == (HANDLE)-1) return 0;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    DWORD pid = GetCurrentProcessId(), me = GetCurrentThreadId();
    static DWORD ids[GL_MAXTH];
    uint32_t nid = 0;
    if (Thread32First(s, &te)) do {                     /* collect ids first, pause after closing the snapshot */
        if (te.th32OwnerProcessID == pid && te.th32ThreadID != me && te.th32ThreadID != skip2 && nid < GL_MAXTH) ids[nid++] = te.th32ThreadID;
    } while (Thread32Next(s, &te));
    CloseHandle(s);
    for (uint32_t i = 0; i < nid; i++) {
        HANDLE h = OpenThread(0x0002 /* suspend/resume */, 0, ids[i]);
        if (!h) continue;
        if (SuspendThread(h) != (DWORD)-1) gl_th[gl_nth++] = h; else CloseHandle(h);
    }
    return gl_nth;
}
static void gl_resume(void)
{
    for (uint32_t i = 0; i < gl_nth; i++) { ResumeThread(gl_th[i]); CloseHandle(gl_th[i]); }
    gl_nth = 0;
}

/* redirect sites (rel32 of E8 calls to 'expect') to 'target'; all or nothing */
static int gl_patch(const uintptr_t *site, uint32_t n, uintptr_t expect, void *target, DWORD skip2, char *why)
{
    uintptr_t lo = site[0], hi = site[0];
    for (uint32_t i = 0; i < n; i++) {
        if (site[i] < lo) lo = site[i];
        if (site[i] > hi) hi = site[i];
        if (!mem_readable(site[i], 5) || *(const BYTE *)site[i] != 0xe8 || site[i] + 5 + *(const int32_t *)(site[i] + 1) != expect) {
            wsprintfA(why, "site %p is not 'call %p'", (void *)site[i], (void *)expect);
            return 0;
        }
    }
    uintptr_t tr = gl_trampoline(lo, target);
    if (!tr) { wsprintfA(why, "no trampoline within 2 GB"); return 0; }
    for (uint32_t i = 0; i < n; i++) {
        intptr_t d = (intptr_t)tr - (intptr_t)(site[i] + 5);
        if (d > 0x7fffffff || d < -0x7fffffffLL) { wsprintfA(why, "trampoline out of reach"); return 0; }
    }
    uintptr_t plo = lo & ~(uintptr_t)0xfff, phi = (hi + 5 + 0xfff) & ~(uintptr_t)0xfff;
    DWORD old = 0, tmp;
    if (!VirtualProtect((void *)plo, phi - plo, 0x40, &old)) { wsprintfA(why, "VirtualProtect failed"); return 0; }
    uint32_t paused = gl_pause(skip2);
    int ok = 1;
    for (uint32_t i = 0; i < n; i++)                     /* verify again while everything is paused */
        if (*(const BYTE *)site[i] != 0xe8 || site[i] + 5 + *(const int32_t *)(site[i] + 1) != expect) ok = 0;
    if (ok)
        for (uint32_t i = 0; i < n; i++) *(volatile int32_t *)(site[i] + 1) = (int32_t)((intptr_t)tr - (intptr_t)(site[i] + 5));
    FlushInstructionCache((HANDLE)-1, (const void *)plo, phi - plo);
    gl_resume();
    VirtualProtect((void *)plo, phi - plo, old, &tmp);
    wsprintfA(why, "%u threads paused, trampoline %p, %s", paused, (void *)tr, ok ? "patched" : "changed meanwhile, not patched");
    return ok;
}

static void gl_load(void)
{
    static char buf[4096];
    uint32_t v;
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
        if (tt_find_value(buf, r, "coop_display_fix=", &v)) gl_fix = v ? 1 : 0;
        if (tt_find_value(buf, r, "coop_display_pos=", &v)) gl_pos = v;
        if (tt_find_hex(buf, r, "coop_display_sites=", &v)) gl_mask = v & 0xbf;
    }
    if (gl_pos == 0xffffffff)                             /* default: lowest mapped position */
        for (int k = 0; k < SC_MAXCTL; k++) if (sc_pos[k] < gl_pos) gl_pos = sc_pos[k];
}

/* F5 (research build), v0.39: ON (map + target board + sight cursor) <-> OFF.
 * Round 38: map + sight cursor = mini-map steady, movement normal; map + target board = white flashing on the
 * characters gone, movement normal. Round 35 (all sites) had movement "pulled back": v0.39 tests the two
 * together. */
static void display_fix_cycle(void)
{
    char b[200];
    if (!gl_applied) { logs("=== F5: display fix not applied yet (it is applied when co-op first starts) ===\r\n"); return; }
    gl_mask = gl_mask ? 0 : GL_BOTH;
    wsprintfA(b, "=== F5: display fix now %s (t=%lu ms) ===\r\n", gl_mode_name(gl_mask), (unsigned long)GetTickCount64());
    logs(b);
}

/* hotkey thread: apply once, when co-op first runs; then report the UI calls every 10 s of co-op */
static void display_fix_poll(void)
{
    char b[300], why[160];
    if (gl_applied && cs_on) {
        uint64_t now = GetTickCount64();
        if (now - gl_report_t >= 10000) {
            gl_report_t = now;
            wsprintfA(b, "  [display] %s; calls: mini-map %ld %ld %ld %ld, sight cursor %ld %ld, sight outline %ld, target board %ld; steady %ld of %ld\r\n",
                      gl_mode_name(gl_mask), gl_cnt[0], gl_cnt[1], gl_cnt[2], gl_cnt[3], gl_cnt[4], gl_cnt[5], gl_cnt[6], gl_cnt[7], gl_shown, gl_calls);
            logs(b);
        }
    }
    if (gl_done || !cs_on) return;
    gl_done = 1;
    gl_load();
    if (!gl_fix) { logs("  display fix: off (coop_display_fix=0)\r\n"); return; }
    uintptr_t s[GL_NSITES];
    for (uint32_t i = 0; i < GL_NSITES; i++) s[i] = game_base + gl_sites[i];
    int ok = gl_patch(s, GL_NSITES, game_base + GL_RVA, (void *)gl_display, cs_tid, why);
    gl_applied = ok;
    gl_report_t = GetTickCount64();
#ifdef SO4COOP_RELEASE
#define GL_F5_HINT ""
#else
#define GL_F5_HINT "; F5 = on/off"
#endif
    wsprintfA(b, "  display fix: %s; %lu UI call sites patched, steady display leader = party position %lu during co-op, %s" GL_F5_HINT " (%s)\r\n",
              ok ? "ON" : "NOT applied", (unsigned long)GL_NSITES, (unsigned long)gl_pos, gl_mode_name(gl_mask), why);
    logs(b);
}

/* ---- self-test: patch a call inside the probe itself, the same way ---- */
static __attribute__((noinline)) uintptr_t gl_fake_getleader(uintptr_t x) { return x + 1; }
static __attribute__((noinline)) uintptr_t gl_fake_replacement(uintptr_t x) { return x + 1000; }
static __attribute__((noinline)) uintptr_t gl_fake_caller(uintptr_t x) { return gl_fake_getleader(x) * 2; }

__declspec(dllexport) void WINAPI so4coop_patchtest(void)
{
    char b[300], why[160];
    uintptr_t (*volatile call)(uintptr_t) = gl_fake_caller;     /* no compile-time folding */
    volatile uintptr_t arg = 1;
    uintptr_t before = call(arg), site = 0;
    for (const BYTE *p = (const BYTE *)gl_fake_caller; p < (const BYTE *)gl_fake_caller + 64; p++)
        if (p[0] == 0xe8 && (uintptr_t)p + 5 + *(const int32_t *)(p + 1) == (uintptr_t)gl_fake_getleader) { site = (uintptr_t)p; break; }
    if (!site) { logs("patch self-test: call site not found (compiler inlined it?)\r\n"); return; }
    int ok = gl_patch(&site, 1, (uintptr_t)gl_fake_getleader, (void *)gl_fake_replacement, 0, why);
    uintptr_t after = call(arg);
    wsprintfA(b, "patch self-test: before %lu (expect 4), after %lu (expect 2002): %s -> %s\r\n", (unsigned long)before, (unsigned long)after, why,
              ok && before == 4 && after == 2002 ? "WORKS" : "FAILED");
    logs(b);
}
