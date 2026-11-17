/*
 * so4coop probe - write test (v0.13).  Included by probe.c after allrtti.c.
 *
 * Runs 11-12: CBattleMainTask+0x3c4 = 0-based party position of the controlled member
 * (+0x3c0 = highest position); CStatusBoardWindow+0x1f0 mirrors it, with the highlight x at
 * +0x1f4 (x = 70 + 283 * position).
 * F8 (only in a battle, with a controlled character): writes ONE byte, position + 1 (wrapping),
 * then logs after 0.2 s, 1 s and 3 s whether the game kept the value and whether the camera
 * block (+0x129a0) and the status board followed. This is the probe's only write into game
 * memory; nothing else is written and no file of the game is touched.
 */

int _fltused = 0x9875;          /* no CRT: the MSVC target references this when floats are used */

#define VK_F8 0x77
#define MT_MAX 0x3c0
#define MT_POS 0x3c4

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static uintptr_t find_board(void)
{
    if (!ball_done) ball_init();
    if (!nbvt) return 0;
    nbobj = bobj_over = 0;
    for (uint32_t k = 0; k < nbcls; k++) bcount[k] = 0;
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, ball_objs, 0);
    for (uint32_t i = 0; i < nbobj; i++)
        if (str_eq(bcls[bobj[i].cls].name, "CStatusBoardWindow@Battle@@")) return bobj[i].addr;
    return 0;
}

static const char *controlled_now(uintptr_t *who)
{
    for (uint32_t i = 0; i < nobj; i++)
        if (objs[i].cls <= 8 && mem_readable(objs[i].addr + 0x129a0, 8) && *(const uintptr_t *)(objs[i].addr + 0x129a0)) {
            if (who) *who = objs[i].addr;
            return CLASSES[objs[i].cls];
        }
    if (who) *who = 0;
    return "(none: camera block empty)";
}

static void log_state(const char *tag, uintptr_t mt, uintptr_t board)
{
    char b[300], xs[32] = "-";
    uint32_t bpos = 0xffffffff;
    if (board && mem_readable(board + 0x1f0, 8)) {
        bpos = *(const uint32_t *)(board + 0x1f0);
        int x = (int)*(const float *)(board + 0x1f4);
        wsprintfA(xs, "%d", x);
    }
    wsprintfA(b, "  %-12s main+0x3c4=%u (max %u) | board pos=%ld x=%s | controlled: %s\r\n", tag,
              (unsigned)*(const BYTE *)(mt + MT_POS), (unsigned)*(const BYTE *)(mt + MT_MAX),
              bpos == 0xffffffff ? -1L : (long)bpos, xs, controlled_now(0));
    logs(b);
}

/* v0.14: OFF. Run 13 showed CBattleMainTask+0x3c4 does not follow the switch in every battle
 * (0/256 there); the matches of runs 11-12 were another state. Never write it. */
#define WRITE_TEST 0

static void write_test(void)
{
    char b[300];
    logs("=== WRITE TEST (F8) ===\r\n");
    if (!WRITE_TEST) { logs("  disabled since v0.14 (run 13: wrong target); nothing written\r\n"); return; }
    if (!rtti_done) rtti_init();
    nobj = 0;
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    own_stack();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    uintptr_t mt = 0;
    for (uint32_t i = 0; i < nobj; i++) {
        if (objs[i].cls != 21 /* CBattleMainTask */ || !mem_readable(objs[i].addr, MT_POS + 4)) continue;
        uint32_t mx = *(const uint32_t *)(objs[i].addr + MT_MAX), cur = *(const uint32_t *)(objs[i].addr + MT_POS);
        wsprintfA(b, "  CBattleMainTask@%p +0x3c0=%lu +0x3c4=%lu\r\n", (void *)objs[i].addr, (unsigned long)mx, (unsigned long)cur);
        logs(b);
        if (mx >= 1 && mx <= 3 && cur <= mx) mt = objs[i].addr;
    }
    if (!mt) { logs("  no CBattleMainTask with a valid position: not written (be in a battle)\r\n"); return; }
    uintptr_t who;
    controlled_now(&who);
    if (!who) { logs("  no controlled character (mid-switch?): not written\r\n"); return; }
    uintptr_t board = find_board();
    log_state("before", mt, board);
    BYTE cur = *(const BYTE *)(mt + MT_POS), mx = *(const BYTE *)(mt + MT_MAX);
    BYTE nw = (BYTE)((cur + 1) % (mx + 1));
    *(volatile BYTE *)(mt + MT_POS) = nw;
    wsprintfA(b, "  WROTE CBattleMainTask@%p +0x3c4: %u -> %u\r\n", (void *)mt, (unsigned)cur, (unsigned)nw);
    logs(b);
    static const DWORD waits[3] = { 200, 800, 2000 };
    static const char *const tags[3] = { "after 0.2 s", "after 1 s", "after 3 s" };
    for (int i = 0; i < 3; i++) {
        Sleep(waits[i]);
        if (!mem_readable(mt, MT_POS + 4)) { logs("  CBattleMainTask no longer readable (battle ended?)\r\n"); return; }
        log_state(tags[i], mt, board);
    }
}

/* test hook (not used by the game): run the write test now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_writetest(void) { write_test(); }
