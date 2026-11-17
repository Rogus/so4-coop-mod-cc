/*
 * so4coop probe - write test (v0.20).  Included by probe.c after allrtti.c.
 *
 * The battle leader (the only member the player controls; L1/R1 changes it) is kept in a small
 * block reached through CBattleMainTask+0x68: [0] = highest party position, [4] = leader position.
 * Run 16 found the values at main+0x3a0 only because the block happened to sit right after the
 * battle manager; in run 20 +0x68 pointed to main+0x3e0 (+0x60 = the same base in both: it looks
 * like a small stack of state entries). Reading main+0x3a0 directly was wrong in runs 13/17/19/20.
 * CStatusBoardWindow+0x1f0 shows the same position (highlight x at +0x1f4 = 70 + 283 * position).
 *
 * F8 writes ONE byte, leader position + 1 (wrapping), only if three independent checks agree:
 *   1. block = *(main+0x68) is readable, within 0x200 bytes after *(main+0x60), [0] is 1..3 and
 *      [4] <= [0];
 *   2. the status board shows the same position;
 *   3. [0] == number of party characters in memory - 1 (counted by RTTI class).
 * Then logs block, board, CThisIs target and camera focus before and 0.2 s / 1 s / 3 s after.
 * This is the probe's only write into game memory; no game file is touched.
 */

int _fltused = 0x9875;          /* no CRT: the MSVC target references this when floats are used */

#define WRITE_TEST 1

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

/* camera focus (the +0x129a0 block): logged for reference only, it is not the leader */
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
    const char *cn = class_of(p);                        /* "CPlayerEdge@Battle@@" ... */
    if (!cn || cn[0] != 'C' || cn[1] != 'P' || cn[2] != 'l' || cn[3] != 'a' || cn[4] != 'y' || cn[5] != 'e' || cn[6] != 'r') return 0;
    if (str_eq(cn, "CPlayerChr@Battle@@") || str_eq(cn, "CPlayerThinkChr@Battle@@") || str_eq(cn, "CPlayerWireframeEdge@Battle@@")) return 0;
    return cn;
}

/* number of distinct party character objects found by the all-class pass */
static uint32_t party_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < nbobj; i++) if (party_name(bobj[i].addr)) n++;
    return n;
}

/* first CThisIs pointing to a party character (reference only: run 20 had it pointing to 0) */
static const char *thisis_target(void)
{
    for (uint32_t i = 0; i < nbobj; i++) {
        if (!str_eq(bcls[bobj[i].cls].name, "CThisIs@Battle@@") || !mem_readable(bobj[i].addr + 0x38, 8)) continue;
        const char *pn = party_name(*(const uintptr_t *)(bobj[i].addr + 0x38));
        if (pn) return pn;
    }
    return "-";
}

static void log_state(const char *tag, uintptr_t blk, uintptr_t board)
{
    char b[320], bs[48] = "-";
    if (board && mem_readable(board + 0x1f0, 8))
        wsprintfA(bs, "%lu (x=%d)", (unsigned long)*(const uint32_t *)(board + 0x1f0), (int)*(const float *)(board + 0x1f4));
    wsprintfA(b, "  %-12s block leader=%u (max %u) | board %s | CThisIs -> %s | camera focus: %s\r\n", tag,
              (unsigned)*(const BYTE *)(blk + 4), (unsigned)*(const BYTE *)blk, bs, thisis_target(), controlled_now(0));
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
    uintptr_t blk = 0;
    for (uint32_t i = 0; i < nobj && !blk; i++) {
        uintptr_t mt = objs[i].addr;
        if (objs[i].cls != 21 /* CBattleMainTask */ || !mem_readable(mt + 0x60, 16)) continue;
        uintptr_t base = *(const uintptr_t *)(mt + 0x60), p = *(const uintptr_t *)(mt + 0x68);
        int ok = p >= base && p - base < 0x200 && mem_readable(p, 8);
        uint32_t mx = ok ? *(const uint32_t *)p : 0xffffffff, cur = ok ? *(const uint32_t *)(p + 4) : 0xffffffff;
        wsprintfA(b, "  CBattleMainTask@%p +0x60=%p +0x68=%p -> max=%ld leader=%ld\r\n", (void *)mt, (void *)base, (void *)p,
                  ok ? (long)mx : -1L, ok ? (long)cur : -1L);
        logs(b);
        if (ok && mx >= 1 && mx <= 3 && cur <= mx) blk = p;
    }
    if (!blk) { logs("  check 1 failed (no valid leader block behind CBattleMainTask+0x68): nothing written\r\n"); return; }
    battle_pass();
    uintptr_t board = battle_find("CStatusBoardWindow@Battle@@");
    BYTE cur = *(const BYTE *)(blk + 4), mx = *(const BYTE *)blk;
    if (!board || !mem_readable(board + 0x1f0, 4) || *(const uint32_t *)(board + 0x1f0) != cur) {
        logs("  check 2 failed (status board missing or showing another position): nothing written\r\n");
        log_state("state", blk, board);
        return;
    }
    uint32_t pc = party_count();
    if (pc != (uint32_t)mx + 1) {
        wsprintfA(b, "  check 3 failed (%lu party characters in memory, block says %u): nothing written\r\n", (unsigned long)pc, (unsigned)mx + 1);
        logs(b);
        log_state("state", blk, board);
        return;
    }
    log_state("before", blk, board);
    BYTE nw = (BYTE)((cur + 1) % (mx + 1));
    *(volatile BYTE *)(blk + 4) = nw;
    wsprintfA(b, "  WROTE leader block %p +4: %u -> %u\r\n", (void *)blk, (unsigned)cur, (unsigned)nw);
    logs(b);
    static const DWORD waits[3] = { 200, 800, 2000 };
    static const char *const tags[3] = { "after 0.2 s", "after 1 s", "after 3 s" };
    for (int i = 0; i < 3; i++) {
        Sleep(waits[i]);
        if (!mem_readable(blk, 8)) { logs("  leader block no longer readable (battle ended?)\r\n"); return; }
        log_state(tags[i], blk, board);
    }
}

/* test hook (not used by the game): run the write test now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_writetest(void) { write_test(); }
