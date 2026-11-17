/*
 * so4coop probe - automatic captures (v0.14).  Included by probe.c after writetest.c.
 *
 * Pressing F9 at the right moment was the weak point (captures mid-switch, too few characters).
 * F7 (in a battle) starts auto-capture: every 250 ms the probe checks which character holds the
 * camera block (+0x129a0); when it changes and then stays the same for 1.5 s, it saves all
 * Battle:: objects (battle_dump) and logs "=== SCAN #n (auto) ===" + "CONTROLLED: ..." in the
 * format analysis/diff_battle.py reads, plus the status board position. Stops after 8 captures
 * or on F7 again. Read-only.
 */

#define AC_MAX 8
#define AC_STABLE_MS 1500
static int ac_on;
static uint32_t ac_count;
static uint64_t ac_last_poll, ac_stable_since;
static uintptr_t ac_cur, ac_captured;

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
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    uint32_t chars = 0;
    for (uint32_t i = 0; i < nobj; i++) if (objs[i].cls <= 8) chars++;
    if (!chars) { logs("=== AUTO-CAPTURE: no party characters found, press F7 during a battle ===\r\n"); return; }
    ac_on = 1; ac_count = 0; ac_cur = 0; ac_captured = 0;
    ac_stable_since = ac_last_poll = GetTickCount64();
    wsprintfA(b, "=== AUTO-CAPTURE ON (F7): %lu party characters; switch with R1 every few seconds ===\r\n", (unsigned long)chars);
    logs(b);
}

static void autocap_poll(void)
{
    char b[300];
    if (!ac_on) return;
    uint64_t now = GetTickCount64();
    if (now - ac_last_poll < 250) return;
    ac_last_poll = now;
    uintptr_t who;
    const char *name = controlled_now(&who);
    if (who != ac_cur) { ac_cur = who; ac_stable_since = now; return; }
    if (!who || who == ac_captured || now - ac_stable_since < AC_STABLE_MS) return;
    wsprintfA(b, "=== SCAN #%u (auto %lu/%u) ===\r\n  CONTROLLED: %s@%p (stable %lu ms)\r\n", ++scan_no,
              (unsigned long)(ac_count + 1), AC_MAX, name, (void *)who, (unsigned long)(now - ac_stable_since));
    logs(b);
    battle_dump();
    for (uint32_t i = 0; i < nbobj; i++)
        if (str_eq(bcls[bobj[i].cls].name, "CStatusBoardWindow@Battle@@") && mem_readable(bobj[i].addr + 0x1f0, 8)) {
            wsprintfA(b, "  status board pos=%lu x=%d\r\n", (unsigned long)*(const uint32_t *)(bobj[i].addr + 0x1f0),
                      (int)*(const float *)(bobj[i].addr + 0x1f4));
            logs(b);
            break;
        }
    ac_captured = who;
    if (++ac_count >= AC_MAX) {
        ac_on = 0;
        wsprintfA(b, "=== AUTO-CAPTURE DONE: %u captures ===\r\n", AC_MAX);
        logs(b);
    }
}

/* test hook (not used by the game): toggle auto-capture now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_autocap(void) { autocap_toggle(); }
