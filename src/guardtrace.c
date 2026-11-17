/*
 * so4coop probe - guard-page trace (v0.22).  Included by probe.c after stickcap.c.
 *
 * Run 22: the left stick is in the input globals (merged X/Y at game+0x1059476/0x1059478,
 * controller records at game+0x1059634 + n*0x6e8 with the stick at +0x14/+0x18), but no code
 * references those addresses directly and no object points to them. To see who reads them:
 * F5 marks the 4 KB page game+0x1059000 as PAGE_GUARD for 2 s. Each access traps; a vectored
 * handler records (instruction, offset in page, read/write), lets the access run with the trap
 * flag set, and re-arms the guard on the following single-step trap. After 2 s (or 200000 hits)
 * the original protection is restored and the readers are logged, stick fields flagged.
 * No thread is suspended, nothing uses ptrace (the cause of the run 7/8 freezes), no game data is
 * written; only the protection of that one page changes for 2 s.
 */

__declspec(dllimport) BOOL WINAPI VirtualProtect(void *, uint64_t, DWORD, DWORD *);

#define GT_PAGE_RVA 0x1059000
#define GT_WINDOW_MS 2000
#define GT_MAX_HITS 200000
#define GT_MAXREC 512
#define GT_MAXPEND 64
#define EXC_GUARD 0x80000001u

typedef struct { volatile long ready, n; uintptr_t rip; uint32_t off, rw; } gt_rec_t;
static gt_rec_t gt_rec[GT_MAXREC];
static volatile long gt_nrec, gt_hits, gt_dropped, gt_late;
static volatile DWORD gt_pend[GT_MAXPEND];
static volatile int gt_active;
static uintptr_t gt_page;
static DWORD gt_old;
static uint64_t gt_t0;
static int gt_veh_added;

static long WINAPI gt_veh(EXC_PTRS *ep)
{
    BYTE *rec = (BYTE *)ep->rec, *c = ep->ctx;
    DWORD code = *(DWORD *)rec;
    if (code == EXC_GUARD && gt_page) {
        uintptr_t addr = *(uintptr_t *)(rec + 0x28), rw = *(uintptr_t *)(rec + 0x20);   /* ExceptionInformation[1], [0] */
        if (addr < gt_page || addr >= gt_page + 0x1000) return 0;                       /* not our page */
        if (!gt_active) { __sync_fetch_and_add(&gt_late, 1); return -1; }              /* guard consumed; just retry */
        __sync_fetch_and_add(&gt_hits, 1);
        uintptr_t rip = Q(c, C_RIP);
        uint32_t off = (uint32_t)(addr - gt_page);
        long n = gt_nrec < GT_MAXREC ? gt_nrec : GT_MAXREC, i;
        for (i = 0; i < n; i++)
            if (gt_rec[i].ready && gt_rec[i].rip == rip && gt_rec[i].off == off) { __sync_fetch_and_add(&gt_rec[i].n, 1); break; }
        if (i == n) {
            long k = __sync_fetch_and_add(&gt_nrec, 1);
            if (k < GT_MAXREC) { gt_rec[k].rip = rip; gt_rec[k].off = off; gt_rec[k].rw = (uint32_t)rw; gt_rec[k].n = 1; gt_rec[k].ready = 1; }
            else __sync_fetch_and_add(&gt_dropped, 1);
        }
        DWORD tid = GetCurrentThreadId();
        for (int j = 0; j < GT_MAXPEND; j++)
            if (__sync_bool_compare_and_swap(&gt_pend[j], 0, tid)) { *(DWORD *)(c + C_EFLAGS) |= 0x100; break; }
        return -1;                                     /* re-run the access (page is unguarded now) */
    }
    if (code == EXC_SINGLE_STEP) {
        DWORD tid = GetCurrentThreadId();
        for (int j = 0; j < GT_MAXPEND; j++)
            if (gt_pend[j] == tid) {
                gt_pend[j] = 0;
                *(DWORD *)(c + C_EFLAGS) &= ~0x100u;
                if (gt_active && gt_hits < GT_MAX_HITS) { DWORD o; VirtualProtect((void *)gt_page, 0x1000, gt_old | PAGE_GUARD, &o); }
                return -1;
            }
    }
    return 0;
}

static int gt_start(uintptr_t page)
{
    char b[160];
    if (gt_active) return 0;
    if (!gt_veh_added) {
        if (!AddVectoredExceptionHandler(1, gt_veh)) { logs("  guard trace: AddVectoredExceptionHandler failed\r\n"); return 0; }
        gt_veh_added = 1;
    }
    memset(gt_rec, 0, sizeof gt_rec);
    gt_nrec = gt_hits = gt_dropped = gt_late = 0;
    for (int j = 0; j < GT_MAXPEND; j++) gt_pend[j] = 0;
    gt_page = page;
    DWORD old;
    if (!VirtualProtect((void *)page, 0x1000, 0x04 /* probe the current protection */, &old)) { logs("  guard trace: VirtualProtect failed\r\n"); gt_page = 0; return 0; }
    gt_old = old;
    gt_t0 = GetTickCount64();
    gt_active = 1;
    VirtualProtect((void *)page, 0x1000, gt_old | PAGE_GUARD, &old);
    wsprintfA(b, "=== GUARD TRACE ON: page %p (protection %#lx) for %u ms ===\r\n", (void *)page, (unsigned long)gt_old, GT_WINDOW_MS);
    logs(b);
    return 1;
}

static void gt_stop_and_report(void)
{
    char b[300];
    gt_active = 0;
    DWORD o;
    VirtualProtect((void *)gt_page, 0x1000, gt_old, &o);
    Sleep(50);
    VirtualProtect((void *)gt_page, 0x1000, gt_old, &o);     /* a late re-arm may have raced with the first restore */
    long n = gt_nrec < GT_MAXREC ? gt_nrec : GT_MAXREC;
    wsprintfA(b, "=== GUARD TRACE OFF after %lu ms: %ld accesses, %ld distinct (instruction, offset), %ld dropped, %ld late ===\r\n",
              (unsigned long)(GetTickCount64() - gt_t0), gt_hits, n, gt_dropped, gt_late);
    logs(b);
    for (int pass = 0; pass < 2; pass++)                 /* stick fields first, then everything else */
        for (long i = 0; i < n; i++) {
            if (!gt_rec[i].ready) continue;
            uint32_t off = gt_rec[i].off;
            int stick = (off >= 0x470 && off < 0x480) || (off >= 0x640 && off < 0x650) || (off >= 0xd28 && off < 0xd38);
            if (stick != (pass == 0)) continue;
            char ra[24];
            fmt_addr(ra, gt_rec[i].rip);
            wsprintfA(b, "  %s %-5s page+%#05lx (game+%#lx) at %s x%ld\r\n", stick ? "STICK" : "     ", gt_rec[i].rw == 1 ? "write" : "read",
                      (unsigned long)off, (unsigned long)(gt_page + off - game_base), ra, gt_rec[i].n);
            logs(b);
        }
    gt_page = 0;
}

/* hotkey thread: F5 starts, this ends the window */
static void guard_poll(void)
{
    if (gt_active && (GetTickCount64() - gt_t0 >= GT_WINDOW_MS || gt_hits >= GT_MAX_HITS)) gt_stop_and_report();
}

/* v0.23: OFF in the game. Run 23: the game died inside the 2 s window (probably the native side of
 * Steam Input writing into the guarded page, outside Windows exception handling). The self-test
 * export below still works on the probe's own page. */
#define GUARD_TRACE 0

static __attribute__((unused)) void guard_trace(void)   /* no hotkey since v0.36 (F5 = display fix modes) */
{
    if (!GUARD_TRACE) { logs("=== GUARD TRACE (F5): disabled since v0.23 (run 23 crashed the game) ===\r\n"); return; }
    gt_start(game_base + GT_PAGE_RVA);
}

/* self-test on a page of the probe itself: a helper thread reads it while guarded */
static __attribute__((aligned(4096))) volatile BYTE gt_test_page[4096];
static volatile int gt_test_go;
static DWORD WINAPI gt_test_reader(LPVOID u)
{
    (void)u;
    volatile long s = 0;
    while (!gt_test_go) Sleep(1);
    for (int i = 0; i < 200; i++) { s += gt_test_page[0x474]; s += gt_test_page[0x10]; Sleep(1); }
    return 0;
}

__declspec(dllexport) void WINAPI so4coop_guardtest(void)
{
    char b[160];
    gt_test_go = 0;
    gt_test_page[0x474] = 1;
    HANDLE h = CreateThread(0, 0, gt_test_reader, 0, 0, 0);
    if (!gt_start((uintptr_t)gt_test_page)) return;
    gt_test_go = 1;
    Sleep(600);
    long hits = gt_hits;
    gt_stop_and_report();
    if (h) CloseHandle(h);
    wsprintfA(b, "guard trace self-test: %ld accesses -> %s\r\n", hits, hits >= 20 ? "WORKS (traps and re-arms)" : "DOES NOT WORK");
    logs(b);
}
