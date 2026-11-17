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

#define SC_MAXCTL 4
#define SC_MAXACT 32
#define SC_LOGMAX 1500
static intptr_t sc_iface;
static pGetConnected sc_connected;
static pGetDigital sc_digital;
static pGetSet sc_getset;
static pActivate sc_activate;
static pPadIndex sc_padindex;
static int sc_tried;
static uint64_t sc_last, sc_t0, sc_last_act;
static uint64_t sc_handles[16];
static int sc_nctl = -1;
static uint8_t sc_prev[SC_MAXCTL][SC_MAXACT + 1];
static uint32_t sc_logged, sc_take, sc_actall, sc_activations;

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
    }
    wsprintfA(b, "  steam controller: take action = %s%lu, activate_all = %lu\r\n", sc_take ? "" : "(not set, calibration) ",
              (unsigned long)sc_take, (unsigned long)sc_actall);
    logs(b);
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

/* called every 30 ms by the hotkey thread (after the 45 s start delay); polls every 60 ms */
static void steamctl_poll(void)
{
    char b[200];
    if (!sc_tried) { sc_init(); sc_load_config(); }
    if (!sc_iface) return;
    uint64_t now = GetTickCount64();
    if (now - sc_last < 60) return;
    sc_last = now;
    int n = sc_connected(sc_iface, sc_handles);
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    if (n != sc_nctl) { sc_nctl = n; sc_describe(n); }
    if (now - sc_last_act >= 2000) { sc_last_act = now; sc_activate_all(n); }
    for (int c = 0; c < n && c < SC_MAXCTL; c++)
        for (uint32_t a = 1; a <= SC_MAXACT; a++) {
            uint16_t d = sc_digital(sc_iface, sc_handles[c], a);
            uint8_t st = (uint8_t)((d & 0xff) ? 1 : 0) | (uint8_t)(((d >> 8) & 0xff) ? 2 : 0);   /* bit0 pressed, bit1 active */
            if (st == sc_prev[c][a]) continue;
            if ((st & 1) != (sc_prev[c][a] & 1) && sc_logged < SC_LOGMAX) {
                sc_logged++;
                wsprintfA(b, "  steamctl t=%6lu ms controller %d action %2lu: %s\r\n", (unsigned long)(now - sc_t0), c + 1, (unsigned long)a,
                          (st & 1) ? "pressed" : "released");
                logs(b);
            }
            if (sc_take == a && (st & 1) && !(sc_prev[c][a] & 1) && c < 2) {
                wsprintfA(b, "=== TAG-TEAM: take action on Steam controller %d ===\r\n", c + 1);
                logs(b);
                tt_take(c);
            }
            sc_prev[c][a] = st;
        }
}

/* test hook (not used by the game): one poll; outside the game steam_api64.dll is absent */
__declspec(dllexport) void WINAPI so4coop_steamtest(void) { steamctl_poll(); }
