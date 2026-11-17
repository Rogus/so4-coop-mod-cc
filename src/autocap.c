/*
 * so4coop probe - automatic captures (v0.15).  Included by probe.c after writetest.c.
 *
 * v0.14 triggered on the camera block (+0x129a0), but run 14 showed it is the camera's focus,
 * not the controlled character: in runs 11-13 the dumps taken ~3 s after each scan often had it
 * on another member or on nobody. The status board position (CStatusBoardWindow+0x1f0, highlight
 * x at +0x1f4 = 70 + 283 * position) changes on the R1 press itself, so it is the trigger and label:
 * F7 (in a battle) starts auto-capture; every 250 ms the probe reads the board position; when it
 * changes and stays the same for 1.5 s it saves all Battle:: objects (battle_dump) and logs
 * "=== SCAN #n (auto) ===" + "  POSITION: p" (+ camera focus for reference). Stops after 8
 * captures or on F7 again. Read-only.
 */

#define AC_MAX 8
#define AC_STABLE_MS 1500
static int ac_on;
static uint32_t ac_count;
static uint64_t ac_last_poll, ac_stable_since;
static uintptr_t ac_board;
static uint32_t ac_cur = 0xffffffff, ac_captured = 0xffffffff;
#define AC_RING 15              /* v0.16: last 1.5 s of 100 ms samples */
#define AC_NEED 12              /* >= 80% of them on one position */
#define AC_TRANS_MAX 400        /* logged position changes per F7 session */
static uint32_t ac_ring[AC_RING], ac_nring, ac_ringi, ac_trans;
static uint64_t ac_t0;

static int board_pos(uint32_t *pos, int *x)
{
    if (!ac_board || !mem_readable(ac_board + 0x1f0, 8)) return 0;
    uint32_t p = *(const uint32_t *)(ac_board + 0x1f0);
    if (p > 7) return 0;
    *pos = p;
    *x = (int)*(const float *)(ac_board + 0x1f4);
    return 1;
}

static void autocap_toggle(void)
{
    char b[200];
    if (ac_on) {
        ac_on = 0;
        wsprintfA(b, "=== AUTO-CAPTURE OFF (F7): %lu captures ===\r\n", (unsigned long)ac_count);
        logs(b);
        return;
    }
    if (!rtti_done) rtti_init();
    nobj = 0;
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    own_stack();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);   /* characters, for the camera note */
    ac_board = find_board();
    uint32_t pos;
    int x;
    if (!board_pos(&pos, &x)) { logs("=== AUTO-CAPTURE: no status board found, press F7 during a battle ===\r\n"); return; }
    ac_on = 1; ac_count = 0; ac_cur = 0xffffffff; ac_captured = 0xffffffff;
    ac_nring = ac_ringi = ac_trans = 0;
    ac_t0 = ac_stable_since = ac_last_poll = GetTickCount64();
    wsprintfA(b, "=== AUTO-CAPTURE ON (F7): status board @%p at position %lu (x=%d); switch with R1 every few seconds ===\r\n",
              (void *)ac_board, (unsigned long)pos, x);
    logs(b);
}

/* v0.16: run 15 took no capture at all, so the position cannot have stayed equal for 1.5 s.
 * Now: a sample every 100 ms, every change logged (capped), and a capture when one position
 * holds >= 80% of the last 15 samples (1.5 s). */
static void autocap_poll(void)
{
    char b[300];
    if (!ac_on) return;
    uint64_t now = GetTickCount64();
    if (now - ac_last_poll < 100) return;
    ac_last_poll = now;
    uint32_t pos;
    int x;
    if (!board_pos(&pos, &x)) return;                        /* unreadable: ignore this sample */
    if (pos != ac_cur) {
        if (ac_trans++ < AC_TRANS_MAX) {
            wsprintfA(b, "  t=%6lu ms board pos %ld -> %lu (x=%d) camera focus: %s\r\n", (unsigned long)(now - ac_t0),
                      ac_cur == 0xffffffff ? -1L : (long)ac_cur, (unsigned long)pos, x, controlled_now(0));
            logs(b);
        }
        ac_cur = pos;
        ac_stable_since = now;
    }
    ac_ring[ac_ringi] = pos;
    ac_ringi = (ac_ringi + 1) % AC_RING;
    if (ac_nring < AC_RING) { ac_nring++; return; }
    uint32_t best = 0, bestn = 0;
    for (uint32_t i = 0; i < AC_RING; i++) {
        uint32_t n = 0;
        for (uint32_t j = 0; j < AC_RING; j++) if (ac_ring[j] == ac_ring[i]) n++;
        if (n > bestn) { bestn = n; best = ac_ring[i]; }
    }
    if (bestn < AC_NEED || best == ac_captured) return;
    pos = best;
    wsprintfA(b, "=== SCAN #%u (auto %lu/%u) ===\r\n  POSITION: %lu (%lu of the last %u samples; camera focus: %s)\r\n", ++scan_no,
              (unsigned long)(ac_count + 1), AC_MAX, (unsigned long)pos, (unsigned long)bestn, AC_RING, controlled_now(0));
    logs(b);
    battle_dump();
    ac_captured = pos;
    if (++ac_count >= AC_MAX) {
        ac_on = 0;
        wsprintfA(b, "=== AUTO-CAPTURE DONE: %u captures ===\r\n", AC_MAX);
        logs(b);
    }
}

/* test hook (not used by the game): toggle auto-capture now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_autocap(void) { autocap_toggle(); }
