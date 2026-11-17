/*
 * so4coop probe - write test (v0.17).  Included by probe.c after allrtti.c.
 *
 * Run 16 (8 auto captures, positions 1,2,3,0 twice): CBattleMainTask+0x3a4 = 0-based party
 * position of the controlled member, +0x3a0 = highest position; CThisIs+0x38 points to the
 * controlled character; CStatusBoardWindow+0x1f0 shows the same position (highlight x at +0x1f4).
 * (v0.13 targeted +0x3c4, which run 13 showed to be something else; that write never happened.)
 *
 * F8 writes ONE byte, +0x3a4 = position + 1 (wrapping), only if three independent checks agree:
 *   1. +0x3a0 is 1..3 and +0x3a4 <= +0x3a0;
 *   2. the status board shows the same position;
 *   3. CThisIs+0x38 points to a party character object.
 * Then logs, before and 0.2 s / 1 s / 3 s after: the field, the board, the CThisIs target and the
 * camera focus. This is the probe's only write into game memory; no game file is touched.
 */

int _fltused = 0x9875;          /* no CRT: the MSVC target references this when floats are used */

#define WRITE_TEST 1
#define MT_MAX 0x3a0
#define MT_POS 0x3a4

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* one pass over memory with the all-class map, then lookups by class name */
static void battle_pass(void)
{
    if (!ball_done) ball_init();
    nbobj = bobj_over = 0;
    for (uint32_t k = 0; k < nbcls; k++) bcount[k] = 0;
    if (nbvt) each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, ball_objs, 0);
}

static uintptr_t battle_find(const char *name)
{
    for (uint32_t i = 0; i < nbobj; i++)
        if (str_eq(bcls[bobj[i].cls].name, name)) return bobj[i].addr;
    return 0;
}

static uintptr_t find_board(void)
{
    battle_pass();
    return battle_find("CStatusBoardWindow@Battle@@");
}

/* camera focus (the +0x129a0 block): logged for reference only, it is not the controlled member */
static const char *controlled_now(uintptr_t *who)
{
    for (uint32_t i = 0; i < nobj; i++)
        if (objs[i].cls <= 8 && mem_readable(objs[i].addr + 0x129a0, 8) && *(const uintptr_t *)(objs[i].addr + 0x129a0)) {
            if (who) *who = objs[i].addr;
            return CLASSES[objs[i].cls];
        }
    if (who) *who = 0;
    return "(none)";
}

static const char *party_name(uintptr_t p)
{
    for (uint32_t i = 0; i < nobj; i++)
        if (objs[i].cls <= 8 && objs[i].addr == p) return CLASSES[objs[i].cls];
    return 0;
}

static void log_state(const char *tag, uintptr_t mt, uintptr_t board, uintptr_t thisis)
{
    char b[320], bs[48] = "-", ts[64] = "-";
    if (board && mem_readable(board + 0x1f0, 8))
        wsprintfA(bs, "%lu (x=%d)", (unsigned long)*(const uint32_t *)(board + 0x1f0), (int)*(const float *)(board + 0x1f4));
    if (thisis && mem_readable(thisis + 0x38, 8)) {
        const char *n = party_name(*(const uintptr_t *)(thisis + 0x38));
        wsprintfA(ts, "%s", n ? n : "(not a party character)");
    }
    wsprintfA(b, "  %-12s main+0x3a4=%u (max %u) | board %s | CThisIs -> %s | camera focus: %s\r\n", tag,
              (unsigned)*(const BYTE *)(mt + MT_POS), (unsigned)*(const BYTE *)(mt + MT_MAX), bs, ts, controlled_now(0));
    logs(b);
}

static void write_test(void)
{
    char b[300];
    logs("=== WRITE TEST (F8) ===\r\n");
    if (!WRITE_TEST) { logs("  disabled; nothing written\r\n"); return; }
    if (!rtti_done) rtti_init();
    nobj = 0;
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    own_stack();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    uintptr_t mt = 0;
    for (uint32_t i = 0; i < nobj; i++) {
        if (objs[i].cls != 21 /* CBattleMainTask */ || !mem_readable(objs[i].addr, MT_POS + 4)) continue;
        uint32_t mx = *(const uint32_t *)(objs[i].addr + MT_MAX), cur = *(const uint32_t *)(objs[i].addr + MT_POS);
        wsprintfA(b, "  CBattleMainTask@%p +0x3a0=%lu +0x3a4=%lu\r\n", (void *)objs[i].addr, (unsigned long)mx, (unsigned long)cur);
        logs(b);
        if (mx >= 1 && mx <= 3 && cur <= mx) mt = objs[i].addr;
    }
    if (!mt) { logs("  check 1 failed (no CBattleMainTask with a valid position): nothing written\r\n"); return; }
    battle_pass();
    uintptr_t board = battle_find("CStatusBoardWindow@Battle@@"), thisis = battle_find("CThisIs@Battle@@");
    BYTE cur = *(const BYTE *)(mt + MT_POS), mx = *(const BYTE *)(mt + MT_MAX);
    if (!board || !mem_readable(board + 0x1f0, 4) || *(const uint32_t *)(board + 0x1f0) != cur) {
        logs("  check 2 failed (status board missing or showing another position): nothing written\r\n");
        if (board) log_state("state", mt, board, thisis);
        return;
    }
    if (!thisis || !mem_readable(thisis + 0x38, 8) || !party_name(*(const uintptr_t *)(thisis + 0x38))) {
        logs("  check 3 failed (CThisIs missing or not pointing to a party character): nothing written\r\n");
        log_state("state", mt, board, thisis);
        return;
    }
    log_state("before", mt, board, thisis);
    BYTE nw = (BYTE)((cur + 1) % (mx + 1));
    *(volatile BYTE *)(mt + MT_POS) = nw;
    wsprintfA(b, "  WROTE CBattleMainTask@%p +0x3a4: %u -> %u\r\n", (void *)mt, (unsigned)cur, (unsigned)nw);
    logs(b);
    static const DWORD waits[3] = { 200, 800, 2000 };
    static const char *const tags[3] = { "after 0.2 s", "after 1 s", "after 3 s" };
    for (int i = 0; i < 3; i++) {
        Sleep(waits[i]);
        if (!mem_readable(mt, MT_POS + 4)) { logs("  CBattleMainTask no longer readable (battle ended?)\r\n"); return; }
        log_state(tags[i], mt, board, thisis);
    }
}

/* test hook (not used by the game): run the write test now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_writetest(void) { write_test(); }
