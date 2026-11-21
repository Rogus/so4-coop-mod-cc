/*
 * so4coop - per-controller input through Steam's controller API (v0.28).  Included after tagteam.c.
 *
 * Runs 24-26: XInput sees no controller under Proton, and with Steam Input on both physical
 * controllers feed the game's controller object 1; with Steam Input off the game gets no input.
 * The exe only references XINPUT1_3.dll (capabilities/rumble) and "SteamController005": the game
 * reads its input through ISteamController. Its steam_api64.dll exports the flat API, so the mod
 * asks Steam directly, per physical controller:
 *   SteamClient() -> SteamAPI_ISteamClient_GetISteamController(client, user, pipe, "SteamController005")
 *   GetConnectedControllers -> one handle per controller; GetDigitalActionData(ctrl, controller, action)
 * Action handles are small sequential numbers, so handles 1..32 are polled on each controller and
 * every press/release is logged with a timestamp (calibration).
 * Run 27: Steam listed 3 controllers but only one reported actions (the game seems to activate its
 * action set on one controller only). v0.28:
 *   - per controller: gamepad index and current action set are logged when the list changes;
 *   - activate_all=1 in so4coop.ini: every 2 s, the action set of the first controller that has one
 *     (the game's) is activated on the other connected controllers, so they report their actions too.
 *     This changes Steam's controller state for the game while it runs (no input is sent);
 *   - take_action=<n> in so4coop.ini: a press of action n on Steam controller k (Steam's order) makes
 *     player k's character the leader (k = 1, 2).
 */

typedef void *(*pSteamClient)(void);
typedef int32_t (*pGetHandle)(void);
typedef intptr_t (*pGetCtl)(intptr_t, int32_t, int32_t, const char *);
typedef int (*pGetConnected)(intptr_t, uint64_t *);
typedef uint16_t (*pGetDigital)(intptr_t, uint64_t, uint64_t);   /* struct {bool bState; bool bActive;} in ax */
typedef uint64_t (*pGetSet)(intptr_t, uint64_t);
typedef void (*pActivate)(intptr_t, uint64_t, uint64_t);
typedef int (*pPadIndex)(intptr_t, uint64_t);
typedef void *(*pGetAnalogFlat)(void *, intptr_t, uint64_t, uint64_t);   /* flat: (ret, instance, controller, action) */
typedef int (*pGetOrigins)(intptr_t, uint64_t, uint64_t, uint64_t, int32_t *);   /* (instance, controller, set, action, origins[8]) */
typedef const char *(*pOriginName)(intptr_t, int32_t);                         /* GetGlyphForActionOrigin / GetStringForActionOrigin */

#define SC_MAXCTL 8                  /* v0.47: 8 (Remote Play Together / Sunshine guests + local pads); a battle has 4 positions */
#define SC_MAXACT 32
#define SC_LOGMAX 1500
static intptr_t sc_iface;
static pGetConnected sc_connected;
static pGetDigital sc_digital;
static pGetSet sc_getset;
static pActivate sc_activate;
static pPadIndex sc_padindex;
static pGetAnalogFlat sc_analog;
static volatile uint64_t sc_active_t[SC_MAXCTL];   /* v0.33: last time each controller had input */
static int sc_tried;
static uint64_t sc_last, sc_t0, sc_last_act;
static uint64_t sc_handles[16];
static int sc_nctl = -1;
static uint8_t sc_prev[SC_MAXCTL][SC_MAXACT + 1];
static uint32_t sc_logged, sc_take, sc_actall, sc_activations, sc_p1 = 1;
static uint32_t sc_pos[SC_MAXCTL] = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };   /* v0.29: steamN_position= */
/* v0.45 (1.4.2): xbox_position=P. Steam numbers the controllers in its own order (round 46: the keyboard's virtual
 * Xbox 360 pad from tools/kbpad.py came out as controller 1, not 4). With xbox_position set, the Xbox controller
 * (found from Steam's button glyph for it, v0.46) plays position P and steamN_position counts only the other
 * controllers, in Steam's order. sc_cfg keeps the configured values, sc_pos is what is in force. */
#define SC_UNSET 0xffffffffu
static uint32_t sc_cfg[SC_MAXCTL] = { SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET }, sc_xbox_pos = SC_UNSET;
/* v0.47 (1.5): coop_join=1. Remote Play Together and Sunshine (big-remote-play) guests arrive as more Steam
 * controllers, in an order that changes, and their virtual pads can be Xbox ones too. In join mode nothing is
 * fixed: a controller takes the first free party position at its first button press in a battle, R3 (take_action)
 * moves it to the next free one. Claims are kept by controller handle for the whole session, so they survive
 * other controllers connecting or leaving; a position held by a disconnected controller is free again.
 * steamN_position and xbox_position are not used in join mode. */
#define SC_JMAX 16
static uint32_t sc_join;
static uint64_t sc_jh[SC_JMAX];
static uint32_t sc_jp[SC_JMAX];
static uint32_t sc_jfull_logged;
static pGetOrigins sc_origins;
static pOriginName sc_glyph, sc_ostring;
static uint8_t sc_kind[16];                         /* 0 = not known yet, 1 = Xbox, 2 = other */
static uint64_t sc_kind_h[16];
static int32_t sc_kind_o[16];
static uint64_t sc_kind_t;
static int sc_mapped;

static int sh_install(void *iface);
static uint32_t sc_hook, sc_leader_only;
static volatile uint64_t sh_leader_h;
static volatile int sh_filter;

static void sc_init(void)
{
    char b[200];
    sc_tried = 1;
    sc_t0 = GetTickCount64();
    static const uint16_t name[] = { 's','t','e','a','m','_','a','p','i','6','4','.','d','l','l',0 };
    HMODULE m = GetModuleHandleW(name);
    if (!m) { logs("  steam controller: steam_api64.dll not loaded\r\n"); return; }
    pSteamClient client = (pSteamClient)GetProcAddress(m, "SteamClient");
    pGetHandle user = (pGetHandle)GetProcAddress(m, "SteamAPI_GetHSteamUser"), pipe = (pGetHandle)GetProcAddress(m, "SteamAPI_GetHSteamPipe");
    pGetCtl getctl = (pGetCtl)GetProcAddress(m, "SteamAPI_ISteamClient_GetISteamController");
    sc_connected = (pGetConnected)GetProcAddress(m, "SteamAPI_ISteamController_GetConnectedControllers");
    sc_digital = (pGetDigital)GetProcAddress(m, "SteamAPI_ISteamController_GetDigitalActionData");
    sc_getset = (pGetSet)GetProcAddress(m, "SteamAPI_ISteamController_GetCurrentActionSet");
    sc_activate = (pActivate)GetProcAddress(m, "SteamAPI_ISteamController_ActivateActionSet");
    sc_padindex = (pPadIndex)GetProcAddress(m, "SteamAPI_ISteamController_GetGamepadIndexForController");
    sc_analog = (pGetAnalogFlat)GetProcAddress(m, "SteamAPI_ISteamController_GetAnalogActionData");
    sc_origins = (pGetOrigins)GetProcAddress(m, "SteamAPI_ISteamController_GetDigitalActionOrigins");
    sc_glyph = (pOriginName)GetProcAddress(m, "SteamAPI_ISteamController_GetGlyphForActionOrigin");
    sc_ostring = (pOriginName)GetProcAddress(m, "SteamAPI_ISteamController_GetStringForActionOrigin");
    if (!client || !user || !pipe || !getctl || !sc_connected || !sc_digital) { logs("  steam controller: exports missing\r\n"); return; }
    void *c = client();
    if (!c) { logs("  steam controller: SteamClient() returned null\r\n"); return; }
    sc_iface = getctl((intptr_t)c, user(), pipe(), "SteamController005");
    wsprintfA(b, "  steam controller: interface %p (SteamController005); action-set calls %s\r\n", (void *)sc_iface,
              sc_getset && sc_activate ? "available" : "missing");
    logs(b);
}

static int sc_find_dec(const char *buf, DWORD n, const char *key, uint32_t *out)
{
    uint32_t k = 0;
    while (key[k]) k++;
    for (DWORD i = 0; i + k < n; i++) {
        DWORD j = 0;
        while (j < k && buf[i + j] == key[j]) j++;
        if (j != k) continue;
        uint32_t v = 0, digits = 0;
        for (DWORD p = i + k; p < n && digits < 4 && buf[p] >= '0' && buf[p] <= '9'; p++, digits++) v = v * 10 + (uint32_t)(buf[p] - '0');
        if (!digits) return 0;
        *out = v;
        return 1;
    }
    return 0;
}

static void sc_load_config(void)
{
    char b[200];
    uint32_t v;
    static char buf[4096];
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
        if (sc_find_dec(buf, r, "take_action=", &v) && v >= 1 && v <= SC_MAXACT) sc_take = v;
        if (tt_find_value(buf, r, "activate_all=", &v)) sc_actall = v ? 1 : 0;
        if (sc_find_dec(buf, r, "p1_steam=", &v) && v >= 1 && v <= SC_MAXCTL) sc_p1 = v;
        if (tt_find_value(buf, r, "hook_steam=", &v)) sc_hook = v ? 1 : 0;
        if (tt_find_value(buf, r, "leader_only_input=", &v)) sc_leader_only = v ? 1 : 0;
        static const char *const keys[SC_MAXCTL] = { "steam1_position=", "steam2_position=", "steam3_position=", "steam4_position=",
                                                     "steam5_position=", "steam6_position=", "steam7_position=", "steam8_position=" };
        for (int k = 0; k < SC_MAXCTL; k++) if (tt_find_value(buf, r, keys[k], &v)) sc_pos[k] = v;
        if (tt_find_value(buf, r, "xbox_position=", &v)) sc_xbox_pos = v;
        if (tt_find_value(buf, r, "coop_join=", &v)) sc_join = v ? 1 : 0;
    }
    if (sc_join) for (int k = 0; k < SC_MAXCTL; k++) sc_pos[k] = SC_UNSET;
    for (int k = 0; k < SC_MAXCTL; k++) sc_cfg[k] = sc_pos[k];
    wsprintfA(b, "  steam controller: take action = %s%lu, player 1 = Steam controller %lu (every other controller = player 2), activate_all = %lu\r\n",
              sc_take ? "" : "(not set, calibration) ", (unsigned long)sc_take, (unsigned long)sc_p1, (unsigned long)sc_actall);
    logs(b);
    for (int k = 0; k < SC_MAXCTL; k++)
        if (sc_pos[k] != 0xffffffff) { wsprintfA(b, "  steam controller %d -> party position %lu\r\n", k + 1, (unsigned long)sc_pos[k]); logs(b); }
    if (sc_join) logs("  coop_join = 1: each controller takes the first free character at its first button press in a battle; R3 = next free character (steamN_position / xbox_position not used)\r\n");
    if (sc_xbox_pos != SC_UNSET && !sc_join) {
        wsprintfA(b, "  xbox_position = %lu: the Xbox controller (keyboard pad) plays position %lu; steamN_position counts the other controllers only (%s)\r\n",
                  (unsigned long)sc_xbox_pos, (unsigned long)sc_xbox_pos, sc_origins ? "origins call available" : "ORIGINS CALL MISSING, steamN_position used as is");
        logs(b);
    }
}

/* v0.46: what kind of controller, from the name of the glyph (icon) Steam uses for one of its buttons.
 * v0.45 used EControllerActionOrigin ranges (PS4 50-113, Xbox 114-191) and round 47 proved them wrong for this
 * Steam: the keyboard's Xbox 360 pad reported origin 246, the DualSense 120, the GameSir 40. The glyph file name
 * (e.g. ...xbox_button_a.png / ...ps4_button_x.png) does not depend on the enum's numbering. */
static int sc_kind_of_name(const char *n)
{
    if (!n || !mem_readable((uintptr_t)n, 1) || !*n) return 0;
    for (int i = 0; n[i] && i < 400; i++) {
        char a = n[i], b2 = n[i + 1], c2 = b2 ? n[i + 2] : 0, d2 = c2 ? n[i + 3] : 0;
        if ((a | 32) == 'x' && (b2 | 32) == 'b' && (c2 | 32) == 'o' && (d2 | 32) == 'x') return 1;
    }
    return 2;
}

/* positions in force: without xbox_position (or before every controller is known) the configured steamN values;
 * with it, the first Xbox controller gets xbox_position and the others take steam1.., steam2.. in Steam's order */
static void sc_remap(int n, const uint8_t *kind, const uint32_t *cfg, uint32_t xpos, uint32_t *out)
{
    int all = 1, phys = 0, xbox = 0;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) if (!kind[c]) all = 0;
    for (int c = 0; c < SC_MAXCTL; c++) out[c] = cfg[c];
    if (xpos == SC_UNSET || !all) return;
    for (int c = 0; c < SC_MAXCTL; c++) out[c] = SC_UNSET;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        if (kind[c] == 1 && !xbox) { xbox = 1; out[c] = xpos; }
        else out[c] = phys < SC_MAXCTL ? cfg[phys++] : SC_UNSET;
    }
}

/* every second until every connected controller is known: ask Steam for the origins of actions 1..16 */
static void sc_kinds_poll(int n, uint64_t now)
{
    char b[220];
    if (sc_xbox_pos == SC_UNSET || !sc_origins || !sc_getset) return;
    int changed = 0;
    for (int c = 0; c < n && c < 16; c++)
        if (sc_kind_h[c] != sc_handles[c]) { sc_kind_h[c] = sc_handles[c]; sc_kind[c] = 0; sc_kind_o[c] = 0; changed = 1; }
    if (changed) sc_mapped = 0;
    if (sc_mapped || now - sc_kind_t < 1000) return;
    sc_kind_t = now;
    int unknown = 0;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        if (sc_kind[c]) continue;
        uint64_t set = sc_getset(sc_iface, sc_handles[c]);
        char list[200];
        int ln = 0;
        list[0] = 0;
        for (uint64_t a = 1; set && a <= 16; a++) {
            int32_t o[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
            if (sc_origins(sc_iface, sc_handles[c], set, a, o) <= 0 || o[0] <= 0) continue;
            if (ln < 180) ln += wsprintfA(list + ln, " %lu:%ld", (unsigned long)a, (long)o[0]);
            if (sc_kind[c]) continue;
            const char *g = sc_glyph ? sc_glyph(sc_iface, o[0]) : 0, *st = sc_ostring ? sc_ostring(sc_iface, o[0]) : 0;
            int k = sc_kind_of_name(g);
            if (!k) k = sc_kind_of_name(st);
            if (k) {
                sc_kind[c] = (uint8_t)k; sc_kind_o[c] = o[0];
                wsprintfA(b, "  controller kind: Steam controller %d action %lu origin %ld glyph \"%.120s\" name \"%.40s\" -> %s\r\n", c + 1, (unsigned long)a,
                          (long)o[0], g && mem_readable((uintptr_t)g, 1) ? g : "", st && mem_readable((uintptr_t)st, 1) ? st : "", k == 1 ? "Xbox" : "other");
                logs(b);
            }
        }
        if (sc_kind[c]) { wsprintfA(b, "    origins (action:origin):%s\r\n", list); logs(b); }
        if (!sc_kind[c] && sc_kind_o[c] != -1) {            /* once per controller: why it is not known */
            sc_kind_o[c] = -1;
            wsprintfA(b, "  controller kind: Steam controller %d not known yet (action set %p, glyph call %s); origins (action:origin):%s\r\n", c + 1,
                      (void *)(uintptr_t)set, sc_glyph ? "ok" : "missing", ln ? list : " none");
            logs(b);
        }
        if (!sc_kind[c]) unknown++;
    }
    if (unknown || !n) return;
    uint32_t out[SC_MAXCTL];
    sc_remap(n, sc_kind, sc_cfg, sc_xbox_pos, out);
    for (int c = 0; c < SC_MAXCTL; c++) sc_pos[c] = out[c];
    sc_mapped = 1;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        wsprintfA(b, "  controller map: Steam controller %d (origin %ld, %s) -> party position %ld\r\n", c + 1, (long)sc_kind_o[c],
                  sc_kind[c] == 1 ? "Xbox, keyboard pad" : "other", sc_pos[c] == SC_UNSET ? -1L : (long)sc_pos[c]);
        logs(b);
    }
}

static void sc_maybe_hook(void)
{
    if (sc_hook && sc_iface && sh_install((void *)sc_iface)) {
        sh_filter = sc_leader_only ? 1 : 0;
        logs(sh_filter ? "  steam hook: leader-only input ON (other controllers are neutral while a mapped character leads)\r\n" : "  steam hook: leader-only input off\r\n");
    }
    else if (sc_iface) logs("  steam hook: off (hook_steam=1 in so4coop.ini to enable)\r\n");
}

static void sc_describe(int n)
{
    char b[200];
    wsprintfA(b, "  steam controller: %d connected\r\n", n);
    logs(b);
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        int idx = sc_padindex ? sc_padindex(sc_iface, sc_handles[c]) : -2;
        uint64_t set = sc_getset ? sc_getset(sc_iface, sc_handles[c]) : 0;
        wsprintfA(b, "    controller %d: handle %p, gamepad index %d, action set %p\r\n", c + 1, (void *)(uintptr_t)sc_handles[c], idx,
                  (void *)(uintptr_t)set);
        logs(b);
    }
}

/* activate_all: give every connected controller the action set the game uses */
static void sc_activate_all(int n)
{
    char b[200];
    if (!sc_actall || !sc_getset || !sc_activate || n < 2) return;
    uint64_t src = 0;
    int from = -1;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        uint64_t s = sc_getset(sc_iface, sc_handles[c]);
        if (s) { src = s; from = c; break; }
    }
    if (!src) return;
    for (int c = 0; c < n && c < SC_MAXCTL; c++) {
        if (c == from || sc_getset(sc_iface, sc_handles[c]) == src) continue;
        sc_activate(sc_iface, sc_handles[c], src);
        if (sc_activations++ < 40) {
            wsprintfA(b, "  steam controller: activated action set %p (from controller %d) on controller %d\r\n", (void *)(uintptr_t)src, from + 1, c + 1);
            logs(b);
        }
    }
}

/* ---- v0.47 join mode ---- */
/* highest party position in the current battle (1..3), 0 = not in a battle */
static uint32_t sc_party_max(void)
{
    uintptr_t mt = tt_main_fast();
    if (!mt || !mem_readable(mt + 0x58, 0x18)) return 0;
    uintptr_t base = *(const uintptr_t *)(mt + 0x60), blk = *(const uintptr_t *)(mt + 0x68);
    if (!(blk >= base && blk - base < 0x200) || !mem_readable(blk, 8)) return 0;
    uint32_t mx = *(const uint32_t *)blk;
    return mx >= 1 && mx <= 3 ? mx : 0;
}

static const char *sc_pos_name(uint32_t p)
{
    uintptr_t mt = tt_main_fast();
    if (!mt || p > 3 || !mem_readable(mt + 0x58, 8)) return "?";
    uintptr_t party = *(const uintptr_t *)(mt + 0x58);
    const char *pc = class_of(party);
    if (!pc || !str_eq(pc, "CChrManager@Battle@@") || !mem_readable(party + 0x5a0 + 8 * p, 8)) return "?";
    const char *nm = party_name(*(const uintptr_t *)(party + 0x5a0 + 8 * p));
    return nm ? nm : "?";
}

static uint32_t sc_jget(uint64_t h)
{
    for (int i = 0; i < SC_JMAX; i++) if (h && sc_jh[i] == h) return sc_jp[i];
    return SC_UNSET;
}

/* is position p held by a connected controller other than h? */
static int sc_jtaken(uint32_t p, uint64_t h, int n)
{
    for (int c = 0; c < n && c < 16; c++)
        if (sc_handles[c] != h && sc_jget(sc_handles[c]) == p) return 1;
    return 0;
}

static void sc_jset(uint64_t h, uint32_t p, int n)
{
    int free_i = -1;
    for (int i = 0; i < SC_JMAX; i++) {
        if (sc_jh[i] == h) { sc_jp[i] = p; return; }
        if (!sc_jh[i] && free_i < 0) free_i = i;
    }
    for (int i = 0; i < SC_JMAX && free_i < 0; i++) {     /* table full: reuse the slot of a controller no longer connected */
        int on = 0;
        for (int c = 0; c < n && c < 16; c++) if (sc_handles[c] == sc_jh[i]) on = 1;
        if (!on) free_i = i;
    }
    if (free_i >= 0) { sc_jh[free_i] = h; sc_jp[free_i] = p; }
}

/* next free position for controller c: from the start (join) or after its current one (R3) */
static uint32_t sc_jnext(int c, int n, uint32_t mx, int cycle)
{
    uint64_t h = sc_handles[c];
    uint32_t cur = sc_jget(h), start = cycle && cur != SC_UNSET ? cur + 1 : 0;
    for (uint32_t i = 0; i <= mx; i++) {
        uint32_t p = (start + i) % (mx + 1);
        if (cycle && p == cur) continue;
        if (!sc_jtaken(p, h, n)) return p;
    }
    return SC_UNSET;
}

static void sc_jclaim(int c, int n, uint32_t mx, int cycle)
{
    char b[220];
    if (!mx) return;                                      /* only in a battle (party size known) */
    uint32_t p = sc_jnext(c, n, mx, cycle);
    if (p == SC_UNSET) {
        if (!cycle && sc_jfull_logged++ < 20) { wsprintfA(b, "  JOIN: Steam controller %d: every character is taken\r\n", c + 1); logs(b); }
        return;
    }
    sc_jset(sc_handles[c], p, n);
    if (c < SC_MAXCTL) sc_pos[c] = p;
    wsprintfA(b, "=== JOIN: Steam controller %d (handle %p) %s position %lu (%s) ===\r\n", c + 1, (void *)(uintptr_t)sc_handles[c],
              cycle ? "switches to" : "joins as", (unsigned long)p, sc_pos_name(p));
    logs(b);
}

/* called every 30 ms by the hotkey thread (after the 45 s start delay); polls every 60 ms */
static void steamctl_poll(void)
{
    char b[200];
    if (!sc_tried) { sc_init(); sc_load_config(); sc_maybe_hook(); }
    if (!sc_iface) return;
    uint64_t now = GetTickCount64();
    if (now - sc_last < 60) return;
    sc_last = now;
    int n = sc_connected(sc_iface, sc_handles);
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    if (n != sc_nctl) { sc_nctl = n; sc_describe(n); }
    if (sc_join) { for (int c = 0; c < SC_MAXCTL; c++) sc_pos[c] = c < n ? sc_jget(sc_handles[c]) : SC_UNSET; }
    else sc_kinds_poll(n, now);
    if (now - sc_last_act >= 2000) { sc_last_act = now; sc_activate_all(n); }
    if (sh_filter) {                                      /* v0.31: which controller is the leader's */
        uint32_t pos;
        uint64_t lh = 0;
        if (tt_leader_pos(&pos))
            for (int k = 0; k < n && k < SC_MAXCTL; k++) if (sc_pos[k] == pos) { lh = sc_handles[k]; break; }
        sh_leader_h = lh;
    }
    for (int c = 0; c < n && c < SC_MAXCTL && sc_analog; c++)      /* v0.33: stick activity */
        for (uint32_t a = 1; a <= 2; a++) {
            struct { int mode; float x, y; uint8_t active; uint8_t pad[3]; } ad;
            ad.x = ad.y = 0.0f;
            sc_analog(&ad, sc_iface, sc_handles[c], a);
            if (ad.x > 0.25f || ad.x < -0.25f || ad.y > 0.25f || ad.y < -0.25f) sc_active_t[c] = now;
        }
    for (int c = 0; c < n && c < SC_MAXCTL; c++)
        for (uint32_t a = 1; a <= SC_MAXACT; a++) {
            uint16_t d = sc_digital(sc_iface, sc_handles[c], a);
            uint8_t st = (uint8_t)((d & 0xff) ? 1 : 0) | (uint8_t)(((d >> 8) & 0xff) ? 2 : 0);   /* bit0 pressed, bit1 active */
            if (st & 1) sc_active_t[c] = now;
            if (st == sc_prev[c][a]) continue;
            if ((st & 1) != (sc_prev[c][a] & 1) && sc_logged < SC_LOGMAX) {
                sc_logged++;
                wsprintfA(b, "  steamctl t=%6lu ms controller %d action %2lu: %s\r\n", (unsigned long)(now - sc_t0), c + 1, (unsigned long)a,
                          (st & 1) ? "pressed" : "released");
                logs(b);
            }
            if (sc_join && (st & 1) && !(sc_prev[c][a] & 1)) {   /* v0.47: join on any press, R3 = next free character */
                if (sc_take == a) sc_jclaim(c, n, sc_party_max(), sc_pos[c] != SC_UNSET);
                else if (sc_pos[c] == SC_UNSET) sc_jclaim(c, n, sc_party_max(), 0);
                sc_prev[c][a] = st;
                continue;
            }
            if (sc_take == a && (st & 1) && !(sc_prev[c][a] & 1)) {
                /* v0.29: run 28 showed one physical controller can appear as two Steam controllers, so
                 * player 1 = the configured Steam controller, every other controller = player 2 */
                if (c < SC_MAXCTL && sc_pos[c] != 0xffffffff) {      /* per-controller position (up to 4 players) */
                    wsprintfA(b, "=== TAG-TEAM: take action on Steam controller %d -> position %lu ===\r\n", c + 1, (unsigned long)sc_pos[c]);
                    logs(b);
                    tt_take_pos(c, sc_pos[c]);
                } else {
                    int player = (uint32_t)(c + 1) == sc_p1 ? 0 : 1;
                    wsprintfA(b, "=== TAG-TEAM: take action on Steam controller %d (player %d) ===\r\n", c + 1, player + 1);
                    logs(b);
                    tt_take(player);
                }
            }
            sc_prev[c][a] = st;
        }
}

/* v0.45 self-test: origin -> kind, and the positions in force for the round-46 layout and others */
__declspec(dllexport) void WINAPI so4coop_maptest(void)
{
    char b[260];
    static const uint32_t cfg[SC_MAXCTL] = { 1, 0, 2, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET, SC_UNSET };   /* GameSir -> Sarah, DualSense -> Edge, 3rd pad -> Arumat */
    static const struct { int n; uint8_t kind[8]; uint32_t xpos; uint32_t want[4]; } t[] = {
        { 3, { 1, 2, 2, 0 }, 3, { 3, 1, 0, SC_UNSET } },               /* round 46: keyboard pad came first */
        { 3, { 2, 1, 2, 0 }, 3, { 1, 3, 0, SC_UNSET } },
        { 4, { 2, 2, 2, 1 }, 3, { 1, 0, 2, 3 } },
        { 3, { 1, 2, 0, 0 }, 3, { 1, 0, 2, SC_UNSET } },               /* one not known yet: configured values */
        { 3, { 1, 2, 2, 0 }, SC_UNSET, { 1, 0, 2, SC_UNSET } },        /* no xbox_position: as before */
    };
    int ok = sc_kind_of_name("C:\\Steam\\tenfoot\\resource\\images\\library\\controller\\api\\xbox_button_logo_a.png") == 1
          && sc_kind_of_name("Xbox 360 A") == 1 && sc_kind_of_name("ps4_button_x.png") == 2 && sc_kind_of_name("ps5_button_cross.png") == 2
          && sc_kind_of_name("") == 0 && sc_kind_of_name(0) == 0 && sc_kind_of_name("XBOX") == 1 && sc_kind_of_name("box") == 2;
    wsprintfA(b, "controller map self-test: kinds from glyph names %s\r\n", ok ? "OK" : "WRONG");
    logs(b);
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        uint32_t out[SC_MAXCTL];
        sc_remap(t[i].n, t[i].kind, cfg, t[i].xpos, out);
        int good = 1;
        for (int c = 0; c < 4; c++) if (out[c] != t[i].want[c]) good = 0;
        for (int c = 4; c < SC_MAXCTL; c++) if (out[c] != SC_UNSET) good = 0;
        wsprintfA(b, "  case %u: positions %ld %ld %ld %ld %s\r\n", i + 1, out[0] == SC_UNSET ? -1L : (long)out[0], out[1] == SC_UNSET ? -1L : (long)out[1],
                  out[2] == SC_UNSET ? -1L : (long)out[2], out[3] == SC_UNSET ? -1L : (long)out[3], good ? "OK" : "WRONG");
        logs(b);
        ok &= good;
    }
    logs(ok ? "controller map self-test: WORKS\r\n" : "controller map self-test: FAILED\r\n");
}

/* v0.47 self-test: join mode with five controllers, one leaving, the order changing */
__declspec(dllexport) void WINAPI so4coop_jointest(void)
{
    char b[200];
    const uint64_t A = 0x1111, B = 0x2222, C = 0x3333, D = 0x4444, E = 0x5555;
    for (int i = 0; i < SC_JMAX; i++) { sc_jh[i] = 0; sc_jp[i] = 0; }
    uint64_t h1[5] = { A, B, C, D, E };
    for (int c = 0; c < 5; c++) sc_handles[c] = h1[c];
    sc_nctl = 5;
    sc_jclaim(0, 5, 3, 0);                 /* A joins   -> 0 */
    sc_jclaim(1, 5, 3, 0);                 /* B joins   -> 1 */
    sc_jclaim(0, 5, 3, 1);                 /* A R3      -> 2 (1 is B's) */
    sc_jclaim(2, 5, 3, 0);                 /* C joins   -> 0 (A left it) */
    sc_jclaim(3, 5, 3, 0);                 /* D joins   -> 3 */
    sc_jclaim(4, 5, 3, 0);                 /* E: all four taken -> none */
    int ok1 = sc_jget(A) == 2 && sc_jget(B) == 1 && sc_jget(C) == 0 && sc_jget(D) == 3 && sc_jget(E) == SC_UNSET;
    uint64_t h2[4] = { A, C, D, E };       /* B disconnects: its character is free again */
    for (int c = 0; c < 4; c++) sc_handles[c] = h2[c];
    sc_nctl = 4;
    sc_jclaim(3, 4, 3, 0);                 /* E joins   -> 1 */
    int ok2 = sc_jget(E) == 1;
    uint64_t h3[4] = { E, D, C, A };       /* Steam changes the order: claims follow the handles */
    for (int c = 0; c < 4; c++) sc_handles[c] = h3[c];
    int ok3 = sc_jget(sc_handles[0]) == 1 && sc_jget(sc_handles[1]) == 3 && sc_jget(sc_handles[2]) == 0 && sc_jget(sc_handles[3]) == 2;
    sc_jclaim(1, 4, 1, 1);                 /* 2-member party (mx 1): D's R3 finds nothing free, keeps 3 */
    int ok4 = sc_jget(D) == 3;
    wsprintfA(b, "join self-test: claims %s, freed on disconnect %s, follow handles %s, full party %s: %s\r\n", ok1 ? "OK" : "WRONG", ok2 ? "OK" : "WRONG",
              ok3 ? "OK" : "WRONG", ok4 ? "OK" : "WRONG", ok1 && ok2 && ok3 && ok4 ? "WORKS" : "FAILED");
    logs(b);
    for (int i = 0; i < SC_JMAX; i++) { sc_jh[i] = 0; sc_jp[i] = 0; }
    sc_nctl = -1;
}

/* test hook (not used by the game): one poll; outside the game steam_api64.dll is absent */
__declspec(dllexport) void WINAPI so4coop_steamtest(void) { steamctl_poll(); }
