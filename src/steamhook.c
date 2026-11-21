/*
 * so4coop - pass-through hook of the game's ISteamController (v0.30, research).  Included after steamctl.c.
 *
 * Goal (input split): learn how the game asks Steam for input, per controller handle or for all
 * controllers at once (handle 0xFFFFFFFFFFFFFFFF), so that later only the leader's controller
 * drives the leader and the other controllers' input can be used elsewhere.
 * Method slots, from the flat thunks in the game's steam_api64.dll: RunFrame +0x10,
 * GetConnectedControllers +0x18, ActivateActionSet +0x30, GetCurrentActionSet +0x38,
 * GetDigitalActionData +0x48, GetAnalogActionData +0x60. The data getters are MSVC member functions
 * returning a struct through a hidden pointer: (this=rcx, ret=rdx, controller=r8, action=r9) -> rax=ret.
 * Hook: the interface object's vtable is copied into the probe, the wrapped slots are replaced, and
 * the object's vtable pointer is pointed at the copy (one 8-byte write into Steam's object; no code
 * is patched). The wrappers only count and pass through. hook_steam=1 in so4coop.ini enables it.
 */

#define SH_SLOTS 40
#define SH_ALL 0xffffffffffffffffull
typedef void *(*pDataM)(void *, void *, uint64_t, uint64_t);
typedef int (*pConnM)(void *, uint64_t *);
typedef void (*pRunM)(void *);
typedef uint64_t (*pSetM)(void *, uint64_t);
typedef void (*pActM)(void *, uint64_t, uint64_t);

static void *sh_orig[SH_SLOTS], *sh_new[SH_SLOTS];
static volatile uint64_t sh_leader_h;       /* v0.31: leader's controller handle (0 = no filtering) */
static volatile int sh_filter;
static volatile long sh_neutral;
static void **sh_obj;
static int sh_on;
static uint64_t sh_t0, sh_last;

enum { SH_RUN, SH_CONN, SH_ACT, SH_SET, SH_DIG, SH_ANA, SH_NM };
static const char *const sh_name[SH_NM] = { "RunFrame", "GetConnectedControllers", "ActivateActionSet",
                                            "GetCurrentActionSet", "GetDigitalActionData", "GetAnalogActionData" };
static volatile long sh_calls[SH_NM];
#define SH_MAXH 8
static volatile uint64_t sh_h[SH_MAXH];
static volatile long sh_hcalls[SH_NM][SH_MAXH + 1];       /* last column: table full */
static volatile long sh_act_seen[2][64];                  /* digital / analog action ids 0..63 */
#define SH_MAXRA 24
static volatile uintptr_t sh_ra[SH_MAXRA];
static volatile long sh_ra_m[SH_MAXRA];

static int sh_mine(void) { return GetCurrentThreadId() == my_tid; }

static void sh_note(int m, uint64_t h, uint64_t act, uintptr_t ra)
{
    __sync_fetch_and_add(&sh_calls[m], 1);
    int i;
    if (m == SH_RUN || m == SH_CONN) goto callers;     /* no controller handle */
    for (i = 0; i < SH_MAXH; i++) {
        if (sh_h[i] == h) break;
        if (sh_h[i] == 0 && __sync_bool_compare_and_swap(&sh_h[i], 0, h)) break;
    }
    __sync_fetch_and_add(&sh_hcalls[m][i < SH_MAXH ? i : SH_MAXH], 1);
    if ((m == SH_DIG || m == SH_ANA) && act < 64) sh_act_seen[m == SH_ANA][act] = 1;
callers:
    for (int k = 0; k < SH_MAXRA; k++) {
        if (sh_ra[k] == ra) break;
        if (sh_ra[k] == 0 && __sync_bool_compare_and_swap(&sh_ra[k], 0, ra)) { sh_ra_m[k] = m; break; }
    }
}

static void sh_run(void *self)
{
    if (!sh_mine()) sh_note(SH_RUN, 0, 0, (uintptr_t)__builtin_return_address(0));
    ((pRunM)sh_orig[2])(self);
}
static int sh_conn(void *self, uint64_t *out)
{
    if (!sh_mine()) sh_note(SH_CONN, 0, 0, (uintptr_t)__builtin_return_address(0));
    return ((pConnM)sh_orig[3])(self, out);
}
static void sh_act(void *self, uint64_t h, uint64_t set)
{
    if (!sh_mine()) sh_note(SH_ACT, h, set, (uintptr_t)__builtin_return_address(0));
    ((pActM)sh_orig[6])(self, h, set);
}
static uint64_t sh_set(void *self, uint64_t h)
{
    if (!sh_mine()) sh_note(SH_SET, h, 0, (uintptr_t)__builtin_return_address(0));
    return ((pSetM)sh_orig[7])(self, h);
}
/* v0.31 leader_only_input: the game's queries on a controller that is not the leader's get
 * "not pressed" / "stick centred" (bActive kept); the mod's own queries are never filtered */
static int sh_block(uint64_t h)
{
    uint64_t l = sh_leader_h;
    return sh_filter && l && h != l && h != SH_ALL;
}
/* v0.44 (1.4.1): last time the game saw a button go down on the leader's controller. The game reads the pad,
 * then acts on it a frame later for whoever leads THEN; switching the leader in between handed the press to the
 * next player too (round 44: Arumat's L2 skill and Sarah's at the same time). coopslice.c waits until such a press
 * has been taken before it switches. SH_NOBODY blocks every controller while the leader is being changed. */
#define SH_NOBODY 1ull
static volatile uint64_t sh_edge_t;
static uint64_t sh_edge_h, sh_edge_bits;
static void *sh_dig(void *self, void *ret, uint64_t h, uint64_t act)
{
    int game = !sh_mine();
    if (game) sh_note(SH_DIG, h, act, (uintptr_t)__builtin_return_address(0));
    void *r = ((pDataM)sh_orig[9])(self, ret, h, act);
    if (game && r && sh_block(h)) { ((uint8_t *)r)[0] = 0; __sync_fetch_and_add(&sh_neutral, 1); }
    else if (game && r && sh_filter && h == sh_leader_h && act < 64) {
        if (h != sh_edge_h) { sh_edge_h = h; sh_edge_bits = 0; }   /* new leader: a held button is a new press for the game */
        uint64_t bit = 1ull << act;
        if (((uint8_t *)r)[0]) { if (!(sh_edge_bits & bit)) { sh_edge_bits |= bit; sh_edge_t = GetTickCount64(); } }
        else sh_edge_bits &= ~bit;
    }
    return r;
}
static void *sh_ana(void *self, void *ret, uint64_t h, uint64_t act)
{
    int game = !sh_mine();
    if (game) sh_note(SH_ANA, h, act, (uintptr_t)__builtin_return_address(0));
    void *r = ((pDataM)sh_orig[12])(self, ret, h, act);
    if (game && r && sh_block(h)) { ((float *)r)[1] = 0.0f; ((float *)r)[2] = 0.0f; __sync_fetch_and_add(&sh_neutral, 1); }
    return r;
}

static int sh_install(void *iface)
{
    char b[200];
    if (sh_on || !iface || !mem_readable((uintptr_t)iface, 8)) return 0;
    void **obj = (void **)iface;
    void **vt = (void **)obj[0];
    if (!mem_readable((uintptr_t)vt, SH_SLOTS * 8)) { logs("  steam hook: vtable not readable\r\n"); return 0; }
    for (int i = 0; i < SH_SLOTS; i++) sh_orig[i] = sh_new[i] = vt[i];
    sh_new[2] = (void *)sh_run; sh_new[3] = (void *)sh_conn; sh_new[6] = (void *)sh_act;
    sh_new[7] = (void *)sh_set; sh_new[9] = (void *)sh_dig; sh_new[12] = (void *)sh_ana;
    sh_obj = obj;
    sh_t0 = sh_last = GetTickCount64();
    sh_on = 1;
    __atomic_store_n(&obj[0], (void *)sh_new, __ATOMIC_SEQ_CST);
    wsprintfA(b, "  steam hook: installed on interface %p (vtable %p -> %p), counting only\r\n", (void *)obj, (void *)vt, (void *)sh_new);
    logs(b);
    return 1;
}

static void sh_report(void)
{
    char b[300], ra[24];
    wsprintfA(b, "  [steam hook t=%lus] calls by the game (not the mod); leader-only filter %s, leader handle %p, neutralised %ld:\r\n",
              (unsigned long)((GetTickCount64() - sh_t0) / 1000), sh_filter ? "ON" : "off", (void *)(uintptr_t)sh_leader_h, sh_neutral);
    logs(b);
    for (int m = 0; m < SH_NM; m++) {
        if (!sh_calls[m]) continue;
        wsprintfA(b, "    %-24s %7ld", sh_name[m], sh_calls[m]);
        logs(b);
        for (int i = 0; i <= SH_MAXH; i++) {
            if (!sh_hcalls[m][i]) continue;
            uint64_t h = i < SH_MAXH ? sh_h[i] : 0;
            if (i == SH_MAXH) wsprintfA(b, " | other handles x%ld", sh_hcalls[m][i]);
            else if (h == SH_ALL) wsprintfA(b, " | ALL controllers x%ld", sh_hcalls[m][i]);
            else wsprintfA(b, " | handle %p x%ld", (void *)(uintptr_t)h, sh_hcalls[m][i]);
            logs(b);
        }
        logs("\r\n");
    }
    for (int t = 0; t < 2; t++) {
        char list[260];
        int n = 0;
        list[0] = 0;
        for (int a = 0; a < 64 && n < 240; a++) if (sh_act_seen[t][a]) n += wsprintfA(list + n, " %d", a);
        if (n) { wsprintfA(b, "    %s actions queried:%s\r\n", t ? "analog" : "digital", list); logs(b); }
    }
    for (int k = 0; k < SH_MAXRA && sh_ra[k]; k++) {
        fmt_addr(ra, sh_ra[k]);
        wsprintfA(b, "    caller %s -> %s\r\n", ra, sh_name[sh_ra_m[k]]);
        logs(b);
    }
}

/* hotkey thread: report every 10 s for the first 2 minutes, then every 60 s */
static void steamhook_poll(void)
{
    if (!sh_on) return;
    uint64_t now = GetTickCount64(), age = now - sh_t0;
    if (now - sh_last >= (age < 120000 ? 10000u : 60000u)) { sh_last = now; sh_report(); }
}

/* ---- self-test: a fake interface with the same member ABI, called the way the game calls it ---- */
static void *fake_dig(void *self, void *ret, uint64_t h, uint64_t act) { (void)self; ((uint8_t *)ret)[0] = (uint8_t)(h == 7 && act == 10); ((uint8_t *)ret)[1] = 1; return ret; }
static void *fake_ana(void *self, void *ret, uint64_t h, uint64_t act) { (void)self; (void)h; ((float *)ret)[1] = (float)act; ((int *)ret)[0] = 1; return ret; }
static int fake_conn(void *self, uint64_t *out) { (void)self; out[0] = 7; out[1] = 9; return 2; }
static void fake_nop(void *self) { (void)self; }
static void *fake_vt[SH_SLOTS];
static void *fake_obj[2];
static DWORD WINAPI fake_game_thread(LPVOID u)
{
    (void)u;
    void **o = (void **)fake_obj;
    uint8_t d[2]; float an[4]; uint64_t hs[16];
    for (int i = 0; i < 100; i++) {
        ((pRunM)((void **)o[0])[2])(o);
        ((pConnM)((void **)o[0])[3])(o, hs);
        ((pDataM)((void **)o[0])[9])(o, d, i & 1 ? SH_ALL : 7, 10);
        ((pDataM)((void **)o[0])[12])(o, an, 9, 3);
    }
    return 0;
}

static volatile uint8_t ft_lead, ft_other, ft_own;
static volatile float ft_ana = 99.0f;
static void *fake_dig_any(void *self, void *ret, uint64_t h, uint64_t act) { (void)self; (void)h; (void)act; ((uint8_t *)ret)[0] = 1; ((uint8_t *)ret)[1] = 1; return ret; }
static void *fake_ana_any(void *self, void *ret, uint64_t h, uint64_t act) { (void)self; (void)h; (void)act; ((int *)ret)[0] = 1; ((float *)ret)[1] = 0.7f; ((float *)ret)[2] = 0.7f; return ret; }
static DWORD WINAPI fake_filter_thread(LPVOID u)
{
    (void)u;
    void **o = (void **)fake_obj;
    uint8_t d[2]; float an[4];
    ((pDataM)((void **)o[0])[9])(o, d, 9, 1); ft_lead = d[0];
    ((pDataM)((void **)o[0])[9])(o, d, 7, 1); ft_other = d[0];
    ((pDataM)((void **)o[0])[12])(o, an, 7, 1); ft_ana = an[2];
    return 0;
}

__declspec(dllexport) void WINAPI so4coop_hooktest(void)
{
    char b[200];
    for (int i = 0; i < SH_SLOTS; i++) fake_vt[i] = (void *)fake_nop;
    fake_vt[3] = (void *)fake_conn; fake_vt[9] = (void *)fake_dig; fake_vt[12] = (void *)fake_ana;
    fake_obj[0] = fake_vt;
    my_tid = GetCurrentThreadId();
    sh_install(fake_obj);
    /* our own call (must not be counted) and a check of the pass-through results */
    uint8_t d[2] = { 0, 0 }; float an[4] = { 0, 0, 0, 0 };
    ((pDataM)((void **)fake_obj[0])[9])(fake_obj, d, 7, 10);
    ((pDataM)((void **)fake_obj[0])[12])(fake_obj, an, 9, 3);
    HANDLE h = CreateThread(0, 0, fake_game_thread, 0, 0, 0);
    Sleep(300);
    if (h) CloseHandle(h);
    sh_filter = 1; sh_leader_h = 9;              /* leader = handle 9: handle 7 must be neutralised */
    sh_orig[9] = (void *)fake_dig_any; sh_orig[12] = (void *)fake_ana_any;   /* fakes that report 'pressed' / stick 0.7 on any controller */
    h = CreateThread(0, 0, fake_filter_thread, 0, 0, 0);
    Sleep(300);
    if (h) CloseHandle(h);
    wsprintfA(b, "steam hook filter self-test: leader 9 digital %u (expect 1), other 7 digital %u (expect 0), other analog y %d (expect 0)\r\n",
              (unsigned)ft_lead, (unsigned)ft_other, (int)ft_ana);
    logs(b);
    {
        uint8_t d2[2] = { 0, 0 };
        my_tid = GetCurrentThreadId();           /* in rundll32 the probe thread may have taken my_tid meanwhile */
        ((pDataM)((void **)fake_obj[0])[9])(fake_obj, d2, 7, 1);
        ft_own = d2[0];
    }
    wsprintfA(b, "steam hook filter self-test: own (mod) query on a non-leader controller %u (expect 1)\r\n", (unsigned)ft_own);
    logs(b);
    /* v0.44: a press on the leader's controller is time-stamped; SH_NOBODY blocks every controller */
    sh_edge_t = 0; sh_edge_h = 0;
    h = CreateThread(0, 0, fake_filter_thread, 0, 0, 0);   /* leader 9 reports 'pressed' -> new press */
    Sleep(300);
    if (h) CloseHandle(h);
    int edge_ok = sh_edge_t != 0 && GetTickCount64() - sh_edge_t < 2000;
    uint64_t e1 = sh_edge_t;
    Sleep(20);
    h = CreateThread(0, 0, fake_filter_thread, 0, 0, 0);   /* still held: not a new press */
    Sleep(300);
    if (h) CloseHandle(h);
    int held_ok = sh_edge_t == e1;
    sh_leader_h = SH_NOBODY;
    h = CreateThread(0, 0, fake_filter_thread, 0, 0, 0);
    Sleep(300);
    if (h) CloseHandle(h);
    wsprintfA(b, "steam hook press-guard self-test: press seen %s, held button not a new press %s, during a switch leader %u other %u (expect 0 0): %s\r\n",
              edge_ok ? "OK" : "WRONG", held_ok ? "OK" : "WRONG", (unsigned)ft_lead, (unsigned)ft_other,
              edge_ok && held_ok && !ft_lead && !ft_other ? "WORKS" : "FAILED");
    logs(b);
    sh_filter = 0; sh_leader_h = 0;
    sh_report();
    wsprintfA(b, "steam hook self-test: digital pass-through %s, analog pass-through %s\r\n", d[0] == 1 && d[1] == 1 ? "OK" : "WRONG",
              (int)an[1] == 3 ? "OK" : "WRONG");
    logs(b);
}
