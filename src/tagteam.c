/*
 * so4coop - tag-team mode (v0.24).  Included by probe.c after guardtrace.c.
 *
 * Built on what works (run 21): writing the leader position in the leader block
 * (*(CBattleMainTask+0x68))[4] switches the battle leader completely (camera, status board,
 * control). Here each player owns a party position; pressing L3 (left stick click) on your
 * controller makes your character the leader. F3 / F4 do the same for player 1 / 2 (testing).
 * The game still merges both controllers, so both pads move whoever is the leader.
 *
 * Controllers are read with the real XInput (our own polling, slots 0-3); the first two connected
 * slots are player 1 and player 2. Optional so4coop.ini next to the DLL:
 *     p1_position=0
 *     p2_position=1
 * Checks before every write (no full-memory scan): CBattleMainTask (cached, verified by RTTI),
 * leader block within 0x200 after *(main+0x60), highest position 1-3, current <= highest,
 * *(main+0x58) is a CChrManager whose members[0..highest] are all party characters, and the target
 * position exists. Only that one byte is written.
 */

__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
#define GENERIC_READ 0x80000000
#define OPEN_EXISTING 3
#define XI_TAKE 0x0080          /* XInput R3 (right stick click); run 25: L3 is lock-on in battle */

/* v0.25: run 24 showed XInput sees no controller under Proton (the game uses Steam Input), so the
 * buttons are read from the game's own controller objects: game+0x1059080 (controller 1) and
 * game+0x1059768 (controller 2), button words at +0x5bc..+0x5c6 (game+0x75c660 ORs these 6 words
 * of controller 2 into controller 1). Every change is logged (calibration); the take button is
 * configured in so4coop.ini as take_word=<0-5> and take_mask=<hex>. */
#define TT_PAD0 0x1059080
#define TT_PADSZ 0x6e8
#define TT_BTN 0x5bc
#define TT_LOGMAX 600
static uint32_t tt_word = 0xffffffff, tt_mask16;
static WORD tt_btn_prev[2][6];
static uint32_t tt_btn_logged;

static int tt_find_hex(const char *buf, DWORD n, const char *key, uint32_t *out)
{
    uint32_t k = 0;
    while (key[k]) k++;
    for (DWORD i = 0; i + k < n; i++) {
        DWORD j = 0;
        while (j < k && buf[i + j] == key[j]) j++;
        if (j != k) continue;
        DWORD p = i + k;
        if (p + 1 < n && buf[p] == '0' && (buf[p + 1] == 'x' || buf[p + 1] == 'X')) p += 2;
        uint32_t v = 0, digits = 0;
        for (; p < n && digits < 8; p++, digits++) {
            char c = buf[p];
            uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A' + 10) : 99;
            if (d > 15) break;
            v = v * 16 + d;
        }
        if (!digits) return 0;
        *out = v;
        return 1;
    }
    return 0;
}

static uint32_t tt_owner[2] = { 0, 1 };
static int tt_ready;
static DWORD tt_slot[2] = { 0xffffffff, 0xffffffff };
static WORD tt_prev[4];
static DWORD tt_mask = 0xffffffff;
static uintptr_t tt_main;
static uint64_t tt_last_scan;

static int tt_find_value(const char *buf, DWORD n, const char *key, uint32_t *out)
{
    uint32_t k = 0;
    while (key[k]) k++;
    for (DWORD i = 0; i + k < n; i++) {
        DWORD j = 0;
        while (j < k && buf[i + j] == key[j]) j++;
        if (j == k && buf[i + k] >= '0' && buf[i + k] <= '3') { *out = (uint32_t)(buf[i + k] - '0'); return 1; }
    }
    return 0;
}

static void tt_load_config(void)
{
    char b[200];
    uint16_t path[520];
    DWORD n = GetModuleFileNameW(self_module, path, 512);
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    path[n] = 0;
    wcat(path, "so4coop.ini");
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h != (HANDLE)-1) {
        static char buf[4096];
        DWORD r = 0;
        ReadFile(h, buf, sizeof buf - 1, &r, 0);
        CloseHandle(h);
        tt_find_value(buf, r, "p1_position=", &tt_owner[0]);
        tt_find_value(buf, r, "p2_position=", &tt_owner[1]);
        uint32_t w;
        if (tt_find_value(buf, r, "take_word=", &w) && w <= 5 && tt_find_hex(buf, r, "take_mask=", &tt_mask16) && tt_mask16 && tt_mask16 <= 0xffff)
            tt_word = w;
    }
    if (tt_word <= 5) wsprintfA(b, "  take button: word %lu mask %#lx (from so4coop.ini)\r\n", (unsigned long)tt_word, (unsigned long)tt_mask16);
    else wsprintfA(b, "  take button not configured yet: every button change of both controllers is logged (calibration)\r\n");
    logs(b);
    wsprintfA(b, "=== TAG-TEAM ready: the take button (R3, set in so4coop.ini after calibration) on your controller takes control (F3 = player 1, F4 = player 2); player 1 = party position %lu, player 2 = position %lu%s ===\r\n",
              (unsigned long)tt_owner[0], (unsigned long)tt_owner[1], h != (HANDLE)-1 ? " (from so4coop.ini)" : " (defaults)");
    logs(b);
}

static uintptr_t tt_find_main(void)
{
    if (tt_main) {
        const char *cn = class_of(tt_main);
        if (cn && str_eq(cn, "CBattleMainTask@Battle@@")) return tt_main;
        tt_main = 0;
    }
    uint64_t now = GetTickCount64();
    if (now - tt_last_scan < 5000) return 0;          /* not in a battle: rescan at most every 5 s */
    tt_last_scan = now;
    if (!rtti_done) rtti_init();
    nobj = 0;
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    own_stack();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    for (uint32_t i = 0; i < nobj; i++) if (objs[i].cls == 21) { tt_main = objs[i].addr; break; }
    return tt_main;
}

static void tt_take_pos(int player, uint32_t target)   /* player: label in the log (1-based when printed) */
{
    char b[300];
    uintptr_t mt = tt_find_main();
    if (!mt || !mem_readable(mt + 0x58, 0x18)) { wsprintfA(b, "  P%d: no battle (nothing written)\r\n", player + 1); logs(b); return; }
    uintptr_t party = *(const uintptr_t *)(mt + 0x58), base = *(const uintptr_t *)(mt + 0x60), blk = *(const uintptr_t *)(mt + 0x68);
    if (!(blk >= base && blk - base < 0x200) || !mem_readable(blk, 8)) { logs("  leader block not valid: nothing written\r\n"); return; }
    uint32_t mx = *(const uint32_t *)blk, cur = *(const uint32_t *)(blk + 4);
    const char *pc = class_of(party);
    if (mx < 1 || mx > 3 || cur > mx || !pc || !str_eq(pc, "CChrManager@Battle@@") || !mem_readable(party + 0x5a0, 8 * (mx + 1))) {
        logs("  leader block or party not as expected: nothing written\r\n");
        return;
    }
    for (uint32_t i = 0; i <= mx; i++)
        if (!party_name(*(const uintptr_t *)(party + 0x5a0 + 8 * i))) { logs("  a party slot is not a character: nothing written\r\n"); return; }
    if (target > mx) { wsprintfA(b, "  P%d owns position %lu but the party has only %lu members\r\n", player + 1, (unsigned long)target, (unsigned long)mx + 1); logs(b); return; }
    const char *who = party_name(*(const uintptr_t *)(party + 0x5a0 + 8 * target));
    if (cur == target) { wsprintfA(b, "  P%d: %s is already the leader\r\n", player + 1, who); logs(b); return; }
    *(volatile BYTE *)(blk + 4) = (BYTE)target;
    wsprintfA(b, "  P%d takes control: position %lu -> %lu (%s)\r\n", player + 1, (unsigned long)cur, (unsigned long)target, who);
    logs(b);
}

/* called every 30 ms by the hotkey thread (after the 45 s start delay) */
static void tagteam_poll(void)
{
    char b[200];
    if (!tt_ready) { tt_ready = 1; tt_load_config(); }
    /* the game's own controller objects (Steam Input fills them) */
    for (int p = 0; p < 2; p++) {
        uintptr_t w = game_base + TT_PAD0 + TT_PADSZ * p + TT_BTN;
        if (!mem_readable(w, 12)) continue;
        WORD cur[6];
        int changed = 0;
        for (int i = 0; i < 6; i++) { cur[i] = ((const WORD *)w)[i]; if (cur[i] != tt_btn_prev[p][i]) changed = 1; }
        if (changed && tt_btn_logged < TT_LOGMAX) {
            tt_btn_logged++;
            wsprintfA(b, "  pad%d buttons: %04x %04x %04x %04x %04x %04x\r\n", p + 1, cur[0], cur[1], cur[2], cur[3], cur[4], cur[5]);
            logs(b);
        }
        if (tt_word <= 5 && (cur[tt_word] & tt_mask16) == tt_mask16 && (tt_btn_prev[p][tt_word] & tt_mask16) != tt_mask16) {   /* all bits held: combinations work */
            wsprintfA(b, "=== TAG-TEAM: take button on controller %d ===\r\n", p + 1);
            logs(b);
            tt_take(p);
        }
        for (int i = 0; i < 6; i++) tt_btn_prev[p][i] = cur[i];
    }
    ENSURE();
    if (!r_GetState) return;
    XINPUT_STATE st[4];
    DWORD mask = 0;
    for (DWORD s = 0; s < 4; s++) if (r_GetState(s, &st[s]) == 0) mask |= 1u << s;
    if (mask != tt_mask) {
        tt_mask = mask;
        tt_slot[0] = tt_slot[1] = 0xffffffff;
        for (DWORD s = 0, p = 0; s < 4 && p < 2; s++) if (mask & (1u << s)) tt_slot[p++] = s;
        wsprintfA(b, "  controllers: XInput slots connected %s%s%s%s -> player 1 = slot %ld, player 2 = slot %ld\r\n",
                  mask & 1 ? "0 " : "", mask & 2 ? "1 " : "", mask & 4 ? "2 " : "", mask & 8 ? "3 " : "",
                  tt_slot[0] == 0xffffffff ? -1L : (long)tt_slot[0], tt_slot[1] == 0xffffffff ? -1L : (long)tt_slot[1]);
        logs(b);
    }
    for (int p = 0; p < 2; p++) {
        DWORD s = tt_slot[p];
        if (s > 3) continue;
        WORD bt = st[s].Gamepad.wButtons;
        if ((bt & XI_TAKE) && !(tt_prev[s] & XI_TAKE)) tt_take(p);
        tt_prev[s] = bt;
    }
}

/* test hook (not used by the game): config + one controller poll + "player 2 takes control" */
__declspec(dllexport) void WINAPI so4coop_tagtest(void) { tagteam_poll(); tt_take(1); }

/* the player's own party position (tt_owner) */
static void tt_take(int player) { tt_take_pos(player, tt_owner[player]); }
