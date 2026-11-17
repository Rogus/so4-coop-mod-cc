/*
 * so4coop probe - stick-labelled captures (v0.21).  Included by probe.c after autocap.c.
 *
 * Static analysis found who the leader is (GetLeader game+0x5000d0 = party->members[block[4]],
 * party = *(CBattleMainTask+0x58), members at +0x5a0, block = *(main+0x68)) but not where the
 * movement input enters. Same method that found the leader block: captures labelled by a known
 * input, then a diff (analysis/diff_stick.py).
 * F6 (in a battle, after the 45 s key delay): one capture per press, labelled with the next
 * direction of: neutral, up, right, down, left, neutral, up, right, down, left. Hold the stick in
 * that direction while pressing F6. Each capture saves: all Battle:: objects (battle_dump), the 24
 * known classes incl. the party characters (dump_objects), the input globals game+0x1059000..
 * 0x105b000 (s<NN>_inputglobals.bin) and logs the leader found the game's own way. Read-only.
 */

#define SC_MAX 10
static const char *const sc_seq[SC_MAX] = { "neutral", "up", "right", "down", "left",
                                             "neutral", "up", "right", "down", "left" };
static uint32_t sc_count;

/* the leader the game's own way (GetLeader): party->members[block[4]] */
static uintptr_t leader_now(uint32_t *pos_out)
{
    for (uint32_t i = 0; i < nobj; i++) {
        uintptr_t mt = objs[i].addr;
        if (objs[i].cls != 21 /* CBattleMainTask */ || !mem_readable(mt + 0x58, 0x18)) continue;
        uintptr_t party = *(const uintptr_t *)(mt + 0x58), base = *(const uintptr_t *)(mt + 0x60), blk = *(const uintptr_t *)(mt + 0x68);
        if (!(blk >= base && blk - base < 0x200) || !mem_readable(blk, 8)) continue;
        uint32_t mx = *(const uint32_t *)blk, pos = *(const uint32_t *)(blk + 4);
        if (mx < 1 || mx > 3 || pos > mx || !mem_readable(party + 0x5a0 + 8 * pos, 8)) continue;
        uintptr_t m = *(const uintptr_t *)(party + 0x5a0 + 8 * pos);
        if (!party_name(m)) continue;
        if (pos_out) *pos_out = pos;
        return m;
    }
    return 0;
}

static void stick_capture(void)
{
    char b[300];
    if (sc_count >= SC_MAX) { logs("=== STICK CAPTURE: all 10 done (restart the game for a new series) ===\r\n"); return; }
    if (!rtti_done) rtti_init();
    nobj = 0;
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    own_stack();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    uint32_t pos = 0;
    uintptr_t ld = leader_now(&pos);
    if (!ld) { logs("=== STICK CAPTURE: no leader found (press F6 during a battle) ===\r\n"); return; }
    const char *lbl = sc_seq[sc_count];
    wsprintfA(b, "=== SCAN #%u (stick %lu/%u) ===\r\n  LABEL: %s\r\n  leader: %s@%p (position %lu)\r\n", ++scan_no,
              (unsigned long)(sc_count + 1), SC_MAX, lbl, party_name(ld), (void *)ld, (unsigned long)pos);
    logs(b);
    dump_objects();                                   /* 24 classes: party characters 256 KB, pads 8 KB */
    battle_dump();                                    /* every Battle:: object, 4 KB */
    uintptr_t g = game_base + 0x1059000;
    if (mem_readable(g, 0x2000)) {
        char nm[48];
        wsprintfA(nm, "\\s%02u_inputglobals.bin", scan_no);
        uint16_t path[700];
        dump_path(path, nm);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != (HANDLE)-1) { DWORD w; WriteFile(h, (const void *)g, 0x2000, &w, 0); CloseHandle(h); }
    }
    sc_count++;
    wsprintfA(b, "  next: hold %s and press F6\r\n", sc_count < SC_MAX ? sc_seq[sc_count] : "(done)");
    logs(b);
}

/* test hook (not used by the game): one stick capture now; outside a battle it must refuse */
__declspec(dllexport) void WINAPI so4coop_stickcap(void) { stick_capture(); }
