/*
 * so4coop probe - hardware watchpoints (v0.7).  Included by probe.c after scan.c.
 *
 * After two F9 scans with different controlled characters, the memory slots
 * that switched from the first character to the second are candidates for
 * "who the player controls". Up to 4 of them get an x64 debug-register
 * watchpoint (8 bytes, read/write) on every game thread. A vectored exception
 * handler records which game instructions read or write them; the hotkey
 * thread logs that and dumps the containing function's code for offline
 * disassembly (the game's code only exists decrypted at runtime).
 * Game memory is never written. F11 = report + remove the watchpoints.
 */

typedef struct { void *rec; BYTE *ctx; } EXC_PTRS;
typedef struct { DWORD dwSize, cntUsage, th32ThreadID, th32OwnerProcessID; long tpBasePri, tpDeltaPri; DWORD dwFlags; } THREADENTRY32;
__declspec(dllimport) void *WINAPI AddVectoredExceptionHandler(DWORD, long (WINAPI *)(EXC_PTRS *));
__declspec(dllimport) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD);
__declspec(dllimport) BOOL WINAPI Thread32First(HANDLE, THREADENTRY32 *);
__declspec(dllimport) BOOL WINAPI Thread32Next(HANDLE, THREADENTRY32 *);
__declspec(dllimport) HANDLE WINAPI OpenThread(DWORD, BOOL, DWORD);
__declspec(dllimport) DWORD WINAPI SuspendThread(HANDLE);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE);
__declspec(dllimport) BOOL WINAPI GetThreadContext(HANDLE, void *);
__declspec(dllimport) BOOL WINAPI SetThreadContext(HANDLE, const void *);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);

#define TH32CS_SNAPTHREAD 4
#define THREAD_ACCESS (0x0002 | 0x0008 | 0x0010 | 0x0040)   /* suspend/resume, get/set context, query */
#define CONTEXT_DEBUG_REGISTERS 0x00100010
#define EXC_SINGLE_STEP 0x80000004
/* x64 CONTEXT offsets */
#define C_FLAGS 0x30
#define C_EFLAGS 0x44
#define C_DR0 0x48            /* Dr0..Dr3 = +0x48..+0x60 */
#define C_DR6 0x68
#define C_DR7 0x70
#define C_RSP 0x98
#define C_RIP 0xf8
#define Q(c, off) (*(uint64_t *)((BYTE *)(c) + (off)))

static __attribute__((aligned(16))) BYTE wctx[0x4d0];   /* only used by the hotkey thread */

#define NW 4
static volatile uintptr_t w_addr[NW];
static volatile uint64_t w_last[NW];
static volatile long w_hits[NW];
static long w_mark[NW];
static int w_rw[NW];                 /* 3 = read/write, 1 = write only */
static char w_desc[NW][160];
static volatile int w_active, w_ever;
static int w_any;
static uint64_t dr7_back;            /* Dr7 read back after the last SetThreadContext */

static uintptr_t code_lo, code_hi;   /* the game's executable section (.data1) */
static const uint32_t *rf_tab;       /* exception directory (RUNTIME_FUNCTION) */
static uint32_t rf_n;

#define MAXHIT 160
typedef struct { volatile long ready, reads, writes; uintptr_t rip; uint32_t w, nstack; DWORD tid; uintptr_t stack[6]; } hit_t;
static hit_t hits[MAXHIT];
static volatile long nhits, hits_dropped;
static uint32_t hits_logged;

#define MAXEV 512
typedef struct { volatile long ready; uint32_t w; DWORD tid; uintptr_t rip; uint64_t oldv, newv, t; } ev_t;
static ev_t evs[MAXEV];
static volatile long nev, ev_dropped, w_evn[NW];
static uint32_t ev_logged;
static volatile long unknown_hits, dr6_empty;
static long unknown_mark;

static DWORD my_tid, test_tid;
#define MAXTID 512
static DWORD armed_tid[MAXTID];
static uint32_t narmed;

static int has(const char *s, const char *sub)
{
    for (; *s; s++) {
        const char *a = s, *b = sub;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

static void fmt_addr(char *out, uintptr_t a)
{
    if (a >= game_base && a < game_end) wsprintfA(out, "game+0x%08lx", (unsigned long)(a - game_base));
    else wsprintfA(out, "%p", (void *)a);
}

/* ---- exception handler (runs on game threads: no locks, no allocation) ---- */
static hit_t *hit_entry(uintptr_t rip, uint32_t w, int *fresh)
{
    long n = nhits;
    if (n > MAXHIT) n = MAXHIT;
    for (long i = 0; i < n; i++)
        if (hits[i].ready && hits[i].rip == rip && hits[i].w == w) return &hits[i];
    long i = __sync_fetch_and_add(&nhits, 1);
    if (i >= MAXHIT) { __sync_fetch_and_add(&hits_dropped, 1); return 0; }
    hits[i].rip = rip; hits[i].w = w;
    *fresh = 1;
    return &hits[i];
}

static long WINAPI watch_veh(EXC_PTRS *ep)
{
    if (*(DWORD *)ep->rec != EXC_SINGLE_STEP || !w_ever) return 0;      /* EXCEPTION_CONTINUE_SEARCH */
    BYTE *c = ep->ctx;
    uint32_t mask = (uint32_t)Q(c, C_DR6) & 0xf;
    if (!mask && (*(DWORD *)(c + C_EFLAGS) & 0x100)) return 0;          /* a real single step (TF): not ours */
    if (!mask && !w_active) return 0;
    uintptr_t rip = Q(c, C_RIP);                                        /* data breakpoints trap AFTER the access */
    int w = -1, wrote = 0;
    for (int i = 0; i < NW; i++) if (mask & (1u << i)) { w = i; break; }
    for (int i = 0; i < NW; i++) {
        uintptr_t a = w_addr[i];
        if (!a) continue;
        uint64_t v = *(volatile uint64_t *)a, old = w_last[i];
        if (v == old) continue;
        w_last[i] = v;
        if (w < 0) w = i;
        if (w == i) wrote = 1;
        if (__sync_fetch_and_add(&w_evn[i], 1) >= MAXEV / NW - 8) { __sync_fetch_and_add(&ev_dropped, 1); continue; }   /* per-watch cap */
        long e = __sync_fetch_and_add(&nev, 1);
        if (e < MAXEV) {
            evs[e].w = (uint32_t)i; evs[e].tid = GetCurrentThreadId(); evs[e].rip = rip;
            evs[e].oldv = old; evs[e].newv = v; evs[e].t = GetTickCount64(); evs[e].ready = 1;
        } else __sync_fetch_and_add(&ev_dropped, 1);
    }
    if (!mask) __sync_fetch_and_add(&dr6_empty, 1);
    if (w < 0) { __sync_fetch_and_add(&unknown_hits, 1); w = NW; }   /* DR6 empty and value unchanged */
    if (w < NW) __sync_fetch_and_add(&w_hits[w], 1);
    int fresh = 0;
    hit_t *h = hit_entry(rip, (uint32_t)w, &fresh);
    if (h) {
        if (fresh) {
            h->tid = GetCurrentThreadId();
            uintptr_t sp = Q(c, C_RSP);
            uint32_t ns = 0;
            if (mem_readable(sp, 0x400))
                for (uintptr_t q = sp; q < sp + 0x400 && ns < 6; q += 8) {
                    uintptr_t v = *(const uintptr_t *)q;
                    if (v >= code_lo && v < code_hi) h->stack[ns++] = v;     /* likely return addresses */
                }
            h->nstack = ns;
            h->ready = 1;
        }
        if (wrote) __sync_fetch_and_add(&h->writes, 1); else __sync_fetch_and_add(&h->reads, 1);
    }
    Q(c, C_DR6) = 0;
    return -1;                                                          /* EXCEPTION_CONTINUE_EXECUTION */
}

/* ---- debug registers on game threads ---- */
static uint64_t make_dr7(void)
{
    uint64_t d = 0;
    for (int i = 0; i < NW; i++)
        if (w_addr[i]) d |= (1ull << (2 * i)) | ((uint64_t)(w_rw[i] & 3) << (16 + 4 * i)) | (2ull << (18 + 4 * i));   /* len 8 */
    return d;
}

static int set_thread_dr(HANDLE h)
{
    if (SuspendThread(h) == (DWORD)-1) return 0;
    memset(wctx, 0, sizeof wctx);
    *(DWORD *)(wctx + C_FLAGS) = CONTEXT_DEBUG_REGISTERS;
    for (int i = 0; i < NW; i++) Q(wctx, C_DR0 + 8 * i) = w_addr[i];
    Q(wctx, C_DR6) = 0;
    Q(wctx, C_DR7) = make_dr7();
    int ok = SetThreadContext(h, wctx) != 0;
    memset(wctx, 0, sizeof wctx);
    *(DWORD *)(wctx + C_FLAGS) = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(h, wctx)) dr7_back = Q(wctx, C_DR7);
    ResumeThread(h);
    return ok;
}

static int tid_armed(DWORD t)
{
    for (uint32_t i = 0; i < narmed; i++) if (armed_tid[i] == t) return 1;
    return 0;
}

/* applies the watch table to every game thread (all = 1) or only to threads not armed yet */
static void arm_threads(int all, uint32_t *ok, uint32_t *fail)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == (HANDLE)-1) return;
    THREADENTRY32 te;
    te.dwSize = sizeof te;
    DWORD pid = GetCurrentProcessId();
    if (Thread32First(s, &te)) do {
        DWORD t = te.th32ThreadID;
        if (te.th32OwnerProcessID != pid || t == my_tid || t == test_tid) continue;
        int known = tid_armed(t);
        if (!all && known) continue;
        HANDLE h = OpenThread(THREAD_ACCESS, 0, t);
        if (!h) { (*fail)++; continue; }
        if (set_thread_dr(h)) { (*ok)++; if (!known && narmed < MAXTID) armed_tid[narmed++] = t; }
        else (*fail)++;
        CloseHandle(h);
    } while (Thread32Next(s, &te));
    CloseHandle(s);
}

static void reset_tables(void)
{
    memset(hits, 0, sizeof hits);
    memset(evs, 0, sizeof evs);
    nhits = hits_dropped = nev = ev_dropped = unknown_hits = dr6_empty = 0;
    for (int i = 0; i < NW; i++) w_evn[i] = 0;
    hits_logged = ev_logged = 0;
}

/* ---- module layout: code range and function bounds from the exception directory ---- */
static void watch_init_module(void)
{
    BYTE *b = (BYTE *)game_base;
    int32_t pe = *(int32_t *)(b + 0x3c);
    uint16_t nsec = *(uint16_t *)(b + pe + 6), optsz = *(uint16_t *)(b + pe + 20);
    BYTE *sec = b + pe + 24 + optsz;
    uint32_t best = 0;
    for (uint16_t i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = *(uint32_t *)(sec + 8), rva = *(uint32_t *)(sec + 12), fl = *(uint32_t *)(sec + 36);
        if ((fl & 0x20000000) && vsize > best) { best = vsize; code_lo = game_base + rva; code_hi = code_lo + vsize; }
    }
    uint32_t exc_rva = *(uint32_t *)(b + pe + 24 + 112 + 3 * 8), exc_size = *(uint32_t *)(b + pe + 24 + 112 + 3 * 8 + 4);
    if (exc_rva && exc_size >= 12 && mem_readable(game_base + exc_rva, exc_size)) {
        rf_tab = (const uint32_t *)(game_base + exc_rva);
        rf_n = exc_size / 12;
    }
    char line[160];
    wsprintfA(line, "watch: code game+0x%08lx-0x%08lx, %lu function entries\r\n", (unsigned long)(code_lo - game_base),
              (unsigned long)(code_hi - game_base), (unsigned long)rf_n);
    logs(line);
}

static int fn_bounds(uintptr_t a, uint32_t *start, uint32_t *end)
{
    if (!rf_tab || a < game_base || a >= game_end) return 0;
    uint32_t rva = (uint32_t)(a - game_base), lo = 0, hi = rf_n;
    const uint32_t *e = 0;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        const uint32_t *x = rf_tab + 3 * m;
        if (rva < x[0]) hi = m; else if (rva >= x[1]) lo = m + 1; else { e = x; break; }
    }
    if (!e) return 0;
    *end = e[1];
    for (int k = 0; k < 8; k++) {                       /* follow chained unwind info to the primary entry */
        const BYTE *ui = (const BYTE *)(game_base + (e[2] & ~1u));
        if (!mem_readable((uintptr_t)ui, 4) || !((ui[0] >> 3) & 4)) break;
        const uint32_t *ch = (const uint32_t *)(ui + 4 + ((ui[2] + 1u) & ~1u) * 2);
        if (!mem_readable((uintptr_t)ch, 12)) break;
        e = ch;
    }
    *start = e[0];
    return 1;
}

/* writes the function around a (or a window) to so4coop_dumps/code_<startrva>.bin */
#define MAXCODE 64
static uint32_t code_done[MAXCODE], ncode;
static void dump_code(uintptr_t a, char *note)
{
    uint32_t s, e;
    uint32_t rva = (uint32_t)(a - game_base);
    if (fn_bounds(a, &s, &e)) {
        if (rva - s > 0x6000) s = rva - 0x4000;
        if (e < rva + 0x40) e = rva + 0x40;
        if (e - s > 0x8000) e = s + 0x8000;
    } else { s = rva > 0x400 ? rva - 0x400 : 0; e = rva + 0x100; }
    wsprintfA(note, "code_%08lx.bin", (unsigned long)s);
    for (uint32_t i = 0; i < ncode; i++) if (code_done[i] == s) return;
    if (ncode >= MAXCODE || !mem_readable(game_base + s, e - s)) { note[0] = 0; return; }
    code_done[ncode++] = s;
    char nm[48];
    wsprintfA(nm, "\\code_%08lx.bin", (unsigned long)s);
    uint16_t path[700];
    dump_path(path, nm);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == (HANDLE)-1) { note[0] = 0; return; }
    DWORD w;
    WriteFile(h, (const void *)(game_base + s), e - s, &w, 0);
    CloseHandle(h);
}

/* ---- self-test: a watchpoint on our own variable, written by a helper thread ---- */
static volatile uint64_t test_var;
static volatile int test_go;
static DWORD WINAPI test_thread(LPVOID u)
{
    (void)u;
    while (!test_go) Sleep(1);
    test_var = 0x1111;
    Sleep(1);
    test_var = 0x2222;
    return 0;
}

static void watch_selftest(void)
{
    char b[256];
    test_go = 0; test_var = 0;
    HANDLE h = CreateThread(0, 0, test_thread, 0, 0, &test_tid);
    if (!h) { logs("watch self-test: CreateThread failed\r\n"); return; }
    Sleep(30);
    reset_tables();
    w_addr[0] = (uintptr_t)&test_var; w_rw[0] = 1; w_last[0] = 0; w_hits[0] = 0;
    w_ever = 1; w_active = 1;
    dr7_back = 0;
    int ok = set_thread_dr(h);
    test_go = 1;
    Sleep(300);
    w_active = 0;
    long n = w_hits[0], ev = nev, empty = dr6_empty;
    w_addr[0] = 0;
    wsprintfA(b, "watch self-test: SetThreadContext %s, Dr7 read back %p, hits %ld (writes seen %ld, DR6 empty %ld) -> %s\r\n",
              ok ? "ok" : "FAILED", (void *)dr7_back, n, ev, empty,
              n >= 2 ? "hardware watchpoints WORK" : "hardware watchpoints DO NOT WORK here");
    logs(b);
    CloseHandle(h);
    test_tid = 0;
    reset_tables();
}

static void watch_setup(void)
{
    my_tid = GetCurrentThreadId();
    watch_init_module();
    if (!AddVectoredExceptionHandler(1, watch_veh)) { logs("watch: AddVectoredExceptionHandler failed\r\n"); return; }
    watch_selftest();
}

/* exported for the test harness */
__declspec(dllexport) void WINAPI so4coop_selftest(void) { watch_setup(); }

/* ---- arming from two F9 scans ---- */
static int cand_prio(const ref_t *r)
{
    if (!r->cls) return (r->slot >= game_base && r->slot < game_end) ? 0 : 3;   /* global first, bare heap later */
    if (has(r->cls, "CCameraUserCtrl") || has(r->cls, "CCameraManager")) return 1;
    if (has(r->cls, "CCameraAngleLock")) return 2;
    if (has(r->cls, "CFaceMorph")) return 5;            /* facial animation: cosmetic */
    return 4;
}

static void describe_ref(char *out, const ref_t *r)
{
    if (!r->cls) {
        if (r->slot >= game_base && r->slot < game_end) wsprintfA(out, "game+0x%08lx (global)", (unsigned long)(r->slot - game_base));
        else wsprintfA(out, "%p (heap, no class)", (void *)r->slot);
    } else wsprintfA(out, "%p in %s@%p+0x%lx", (void *)r->slot, r->cls, (void *)r->base, (unsigned long)(r->slot - r->base));
}

static void watch_disarm(void)
{
    uint32_t ok = 0, fail = 0;
    for (int i = 0; i < NW; i++) w_addr[i] = 0;
    if (w_any) arm_threads(1, &ok, &fail);              /* Dr7 = 0 everywhere */
    w_active = 0;
    w_any = 0;
}

static void watch_arm(ref_t **c, uint32_t n)
{
    char b[300];
    watch_disarm();
    Sleep(50);
    reset_tables();
    narmed = 0;
    unknown_mark = 0;
    for (int i = 0; i < NW; i++) {
        w_addr[i] = (uint32_t)i < n ? c[i]->slot : 0;
        w_rw[i] = 3;
        w_last[i] = w_addr[i] ? *(volatile uint64_t *)w_addr[i] : 0;
        w_hits[i] = 0; w_mark[i] = 0;
        if (w_addr[i]) describe_ref(w_desc[i], c[i]); else w_desc[i][0] = 0;
    }
    if (!n) return;
    uint32_t ok = 0, fail = 0;
    w_ever = 1; w_active = 1; w_any = 1;
    arm_threads(1, &ok, &fail);
    wsprintfA(b, "  watch: %lu watchpoints armed on %lu threads (%lu failed), Dr7 read back %p\r\n",
              (unsigned long)n, (unsigned long)ok, (unsigned long)fail, (void *)dr7_back);
    logs(b);
    for (uint32_t i = 0; i < n; i++) {
        const char *cn = class_of(w_last[i]);
        wsprintfA(b, "    w%lu = %s  (now -> %s)\r\n", (unsigned long)i, w_desc[i], cn ? cn : "?");
        logs(b);
    }
    logs("  watch: now switch characters a few times (R1/L1), move and attack for ~20 s, then F11 and close the game\r\n");
}

/* v0.9: fixed targets, armed once at the first F9 that finds the controlled character.
 * Run 7: the only globals that match the controlled character (3 Edge scans vs 1 Sarah scan)
 * are game+0xf77ed2/ed3 (Edge 2/1, Sarah 3/2) and game+0xf77f00 (Edge 2, Sarah 3).
 * CBtlPad+0x1180 changes all the time: control that game threads do trigger watchpoints.
 * CBtlPad+0x28 holds the stick values (0 -> 32767.0 in run 5): whoever writes it is the input path. */
#define G_SEL_A 0x00f77ed0
#define G_SEL_B 0x00f77f00
static ref_t fixed[NW];
static const char C_GLOBAL_A[] = "global game+0x00f77ed0 (ed2/ed3 = controlled member?)";
static const char C_GLOBAL_B[] = "global game+0x00f77f00 (= controlled member?)";

static void watch_arm_fixed(void)
{
    char b[300];
    uintptr_t pad = 0;
    for (uint32_t i = 0; i < nobj; i++) if (objs[i].cls == K_BTLPAD) { pad = objs[i].addr; break; }
    uint32_t n = 0;
    ref_t *c[NW];
    uintptr_t ga = game_base + G_SEL_A, gb = game_base + G_SEL_B;
    if (mem_readable(ga, 8) && mem_readable(gb, 8)) {
        const BYTE *p = (const BYTE *)ga, *q = (const BYTE *)gb;
        wsprintfA(b, "  watch: globals now ed0: %02x %02x %02x %02x %02x %02x %02x %02x | f00: %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
                  p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);
        logs(b);
        fixed[n].slot = ga; fixed[n].base = ga; fixed[n].cls = C_GLOBAL_A; c[n] = &fixed[n]; n++;
        fixed[n].slot = gb; fixed[n].base = gb; fixed[n].cls = C_GLOBAL_B; c[n] = &fixed[n]; n++;
    } else logs("  watch: globals not readable (different game build?)\r\n");
    if (pad && mem_readable(pad + 0x1180, 8)) {
        fixed[n].slot = pad + 0x1180; fixed[n].base = pad; fixed[n].cls = "CBtlPad@Battle@@ (control: changes constantly)"; c[n] = &fixed[n]; n++;
        fixed[n].slot = pad + 0x28;   fixed[n].base = pad; fixed[n].cls = "CBtlPad@Battle@@ (stick values)"; c[n] = &fixed[n]; n++;
    } else logs("  watch: no CBtlPad in this scan\r\n");
    watch_arm(c, n);
}

static void watch_after_scan(void)
{
    char b[300];
    if (controlled_cur && !w_any) watch_arm_fixed();
    if (!controlled_cur) {
        logs("  watch: no controlled character in this scan (press F9 during a battle, not mid-switch)\r\n");
        return;                                         /* keep the previous reference set (run 7 bug) */
    }
    if (!controlled_prev)
        logs("  watch: references saved; switch character (R1/L1) and press F9 again to arm the watchpoints\r\n");
    else if (controlled_prev == controlled_cur)
        logs("  watch: same controlled character as the previous F9; switch character (R1/L1) and press F9 again\r\n");
    else {
        ref_t *cand[16];
        uint32_t nc = 0;
        for (uint32_t i = 0; i < nref_cur && nc < 16; i++) {
            ref_t *r = &refs_cur[i];
            if (r->target != controlled_cur || (r->cls && has(r->cls, "MessageDispatcher"))) continue;
            for (uint32_t j = 0; j < nref_prev; j++)
                if (refs_prev[j].slot == r->slot) { if (refs_prev[j].target == controlled_prev) cand[nc++] = r; break; }
        }
        for (uint32_t i = 1; i < nc; i++)                 /* stable sort by priority */
            for (uint32_t j = i; j > 0 && cand_prio(cand[j]) < cand_prio(cand[j - 1]); j--) {
                ref_t *t = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = t;
            }
        wsprintfA(b, "  watch: %lu slots followed the switch to the new character:\r\n", (unsigned long)nc);
        logs(b);
        for (uint32_t i = 0; i < nc; i++) {
            char d[200];
            describe_ref(d, cand[i]);
            wsprintfA(b, "    %s%s\r\n", d, w_any || i < NW ? "" : "  (not watched: only 4 debug registers)");
            logs(b);
        }
        (void)cand_prio;                                /* v0.9: listed for the notes only, not armed */
    }
    memcpy(refs_prev, refs_cur, nref_cur * sizeof(ref_t));
    nref_prev = nref_cur;
    controlled_prev = controlled_cur;
}

/* ---- hotkey thread: log new events, throttle, arm new threads ---- */
static uint64_t wp_sec, wp_new, wp_sum;

static void log_hit(hit_t *h)
{
    char b[400], ra[24], code[32], fn[48] = "";
    uint32_t s, e;
    fmt_addr(ra, h->rip);
    if (fn_bounds(h->rip, &s, &e)) wsprintfA(fn, " fn=game+0x%08lx", (unsigned long)s);
    dump_code(h->rip, code);
    wsprintfA(b, "  ACCESS w%c after %s%s tid=%lu %s stack:", h->w < NW ? (char)('0' + h->w) : '?', ra, fn, h->tid, code);
    logs(b);
    for (uint32_t i = 0; i < h->nstack; i++) {
        char sa[24];
        fmt_addr(sa, h->stack[i]);
        wsprintfA(b, " %s", sa);
        logs(b);
        if (i < 2) dump_code(h->stack[i], code);
    }
    logs("\r\n");
}

static void watch_poll(void)
{
    char b[300];
    if (!w_any) return;
    long ne = nev;
    if (ne > MAXEV) ne = MAXEV;
    while (ev_logged < (uint32_t)ne && evs[ev_logged].ready) {
        ev_t *e = &evs[ev_logged++];
        const char *o = class_of(e->oldv), *n = class_of(e->newv);
        char ra[24];
        fmt_addr(ra, e->rip);
        wsprintfA(b, "  WRITE w%lu: %p (%s) -> %p (%s) after %s tid=%lu t=%lu ms\r\n", (unsigned long)e->w,
                  (void *)e->oldv, o ? o : "-", (void *)e->newv, n ? n : "-", ra, e->tid, (unsigned long)e->t);
        logs(b);
    }
    long nh = nhits;
    if (nh > MAXHIT) nh = MAXHIT;
    while (hits_logged < (uint32_t)nh && hits[hits_logged].ready) log_hit(&hits[hits_logged++]);

    uint64_t now = GetTickCount64();
    if (now - wp_sec >= 1000) {                        /* too many reads -> write-only, so the game stays playable */
        wp_sec = now;
        int changed = 0;
        for (int i = 0; i < NW; i++) {
            long d = w_hits[i] - w_mark[i];
            w_mark[i] = w_hits[i];
            if (w_addr[i] && w_rw[i] == 3 && d > 3000) {
                w_rw[i] = 1; changed = 1;
                wsprintfA(b, "  watch: w%d read %ld times in 1 s -> now write-only\r\n", i, d);
                logs(b);
            }
        }
        long du = unknown_hits - unknown_mark;          /* Proton leaves DR6 empty: reads are not attributed */
        unknown_mark = unknown_hits;
        if (du > 3000) {
            for (int i = 0; i < NW; i++) if (w_addr[i] && w_rw[i] == 3) { w_rw[i] = 1; changed = 1; }
            if (changed) { wsprintfA(b, "  watch: %ld unattributed reads in 1 s -> all watchpoints now write-only\r\n", du); logs(b); }
        }
        if (changed) { uint32_t ok = 0, fail = 0; arm_threads(1, &ok, &fail); }
    }
    if (now - wp_new >= 5000) {                        /* threads created after arming */
        wp_new = now;
        uint32_t ok = 0, fail = 0;
        arm_threads(0, &ok, &fail);
        if (ok) { wsprintfA(b, "  watch: armed %lu new threads\r\n", (unsigned long)ok); logs(b); }
    }
    if (now - wp_sum >= 15000) {
        wp_sum = now;
        wsprintfA(b, "  [watch] hits w0=%ld w1=%ld w2=%ld w3=%ld unknown=%ld (DR6 empty %ld) writes=%ld unique=%ld dropped=%ld/%ld\r\n",
                  w_hits[0], w_hits[1], w_hits[2], w_hits[3], unknown_hits, dr6_empty, nev, nhits, hits_dropped, ev_dropped);
        logs(b);
    }
}

static void watch_report(void)
{
    char b[300];
    logs("=== WATCH REPORT (F11) ===\r\n");
    if (!w_any) { logs("  no watchpoints armed (F9, switch character, F9 first)\r\n"); return; }
    watch_poll();
    for (int i = 0; i < NW; i++) {
        if (!w_addr[i]) continue;
        uint64_t v = *(volatile uint64_t *)w_addr[i];
        const char *cn = class_of(v);
        wsprintfA(b, "  w%d %s: %ld hits, %s, now -> %s\r\n", i, w_desc[i], w_hits[i], w_rw[i] == 3 ? "read/write" : "write-only",
                  cn ? cn : "?");
        logs(b);
    }
    long nh = nhits;
    if (nh > MAXHIT) nh = MAXHIT;
    for (long i = 0; i < nh; i++) {
        if (!hits[i].ready) continue;
        char ra[24];
        fmt_addr(ra, hits[i].rip);
        wsprintfA(b, "    w%lu after %s: reads=%ld writes=%ld\r\n", (unsigned long)hits[i].w, ra, hits[i].reads, hits[i].writes);
        logs(b);
    }
    wsprintfA(b, "  write events %ld (dropped %ld), unknown hits %ld, DR6 empty %ld\r\n", nev, ev_dropped, unknown_hits, dr6_empty);
    logs(b);
    /* v0.9: no disarm here. Removing the watchpoints suspends every game thread again, and the
     * game froze right after the F11 of run 7; they go away when the game closes anyway. */
    logs("  watchpoints stay armed until the game closes\r\n");
}
