/*
 * so4coop - per-controller input through Steam's controller API (v0.27).  Included after tagteam.c.
 *
 * Runs 24-26: XInput sees no controller under Proton, and with Steam Input on both physical
 * controllers feed the game's controller object 1; with Steam Input off the game gets no input.
 * The exe only references XINPUT1_3.dll (capabilities/rumble) and "SteamController005": the game
 * reads its input through ISteamController. Its steam_api64.dll exports the flat API, so the mod
 * asks Steam directly, per physical controller:
 *   SteamClient() -> SteamAPI_ISteamClient_GetISteamController(client, user, pipe, "SteamController005")
 *   SteamAPI_ISteamController_GetConnectedControllers -> one handle per physical controller
 *   SteamAPI_ISteamController_GetDigitalActionData(ctrl, controller, action) -> {bState, bActive}
 * Action handles are small sequential numbers handed out when the game registers its actions, so
 * handles 1..32 are polled on each controller and every change is logged (calibration). With
 * take_action=<n> in so4coop.ini, a press of action n on Steam controller k (in Steam's order)
 * makes player k's character the leader. Read-only towards Steam: only queries.
 */

typedef void *(*pSteamClient)(void);
typedef int32_t (*pGetHandle)(void);
typedef intptr_t (*pGetCtl)(intptr_t, int32_t, int32_t, const char *);
typedef int (*pGetConnected)(intptr_t, uint64_t *);
typedef uint16_t (*pGetDigital)(intptr_t, uint64_t, uint64_t);   /* struct {bool bState; bool bActive;} in ax */

#define SC_MAXCTL 4
#define SC_MAXACT 32
#define SC_LOGMAX 800
static intptr_t sc_iface;
static pGetConnected sc_connected;
static pGetDigital sc_digital;
static int sc_tried;
static uint64_t sc_last;
static uint64_t sc_handles[16];
static int sc_nctl = -1;
static uint8_t sc_prev[SC_MAXCTL][SC_MAXACT + 1];
static uint32_t sc_logged, sc_take;

static void sc_init(void)
{
    char b[200];
    sc_tried = 1;
    static const uint16_t name[] = { 's','t','e','a','m','_','a','p','i','6','4','.','d','l','l',0 };
    HMODULE m = GetModuleHandleW(name);
    if (!m) { logs("  steam controller: steam_api64.dll not loaded\r\n"); return; }
    pSteamClient client = (pSteamClient)GetProcAddress(m, "SteamClient");
    pGetHandle user = (pGetHandle)GetProcAddress(m, "SteamAPI_GetHSteamUser"), pipe = (pGetHandle)GetProcAddress(m, "SteamAPI_GetHSteamPipe");
    pGetCtl getctl = (pGetCtl)GetProcAddress(m, "SteamAPI_ISteamClient_GetISteamController");
    sc_connected = (pGetConnected)GetProcAddress(m, "SteamAPI_ISteamController_GetConnectedControllers");
    sc_digital = (pGetDigital)GetProcAddress(m, "SteamAPI_ISteamController_GetDigitalActionData");
    if (!client || !user || !pipe || !getctl || !sc_connected || !sc_digital) { logs("  steam controller: exports missing\r\n"); return; }
    void *c = client();
    if (!c) { logs("  steam controller: SteamClient() returned null\r\n"); return; }
    sc_iface = getctl((intptr_t)c, user(), pipe(), "SteamController005");
    wsprintfA(b, "  steam controller: interface %p (SteamController005)\r\n", (void *)sc_iface);
    logs(b);
}

/* called every 30 ms by the hotkey thread (after the 45 s start delay); polls every 60 ms */
static void steamctl_poll(void)
{
    char b[200];
    if (!sc_tried) {
        sc_init();
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
            if (tt_find_hex(buf, r, "take_action=", &v) && v >= 1 && v <= SC_MAXACT) sc_take = v;
        }
        if (sc_take) wsprintfA(b, "  steam controller: take action = handle %lu (from so4coop.ini)\r\n", (unsigned long)sc_take);
        else wsprintfA(b, "  steam controller: take action not set: every action change per controller is logged (calibration)\r\n");
        logs(b);
    }
    if (!sc_iface) return;
    uint64_t now = GetTickCount64();
    if (now - sc_last < 60) return;
    sc_last = now;
    int n = sc_connected(sc_iface, sc_handles);
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    if (n != sc_nctl) {
        sc_nctl = n;
        wsprintfA(b, "  steam controller: %d connected (handles %p %p %p %p)\r\n", n, (void *)(uintptr_t)(n > 0 ? sc_handles[0] : 0),
                  (void *)(uintptr_t)(n > 1 ? sc_handles[1] : 0), (void *)(uintptr_t)(n > 2 ? sc_handles[2] : 0), (void *)(uintptr_t)(n > 3 ? sc_handles[3] : 0));
        logs(b);
    }
    for (int c = 0; c < n && c < SC_MAXCTL; c++)
        for (uint32_t a = 1; a <= SC_MAXACT; a++) {
            uint16_t d = sc_digital(sc_iface, sc_handles[c], a);
            uint8_t st = (uint8_t)((d & 0xff) ? 1 : 0) | (uint8_t)(((d >> 8) & 0xff) ? 2 : 0);   /* bit0 pressed, bit1 active */
            if (st != sc_prev[c][a]) {
                if ((st & 1) != (sc_prev[c][a] & 1) && sc_logged < SC_LOGMAX) {
                    sc_logged++;
                    wsprintfA(b, "  steamctl controller %d action %2lu: %s\r\n", c + 1, (unsigned long)a, (st & 1) ? "pressed" : "released");
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
}

/* test hook (not used by the game): one poll; outside the game steam_api64.dll is absent */
__declspec(dllexport) void WINAPI so4coop_steamtest(void) { steamctl_poll(); }
