/*
 * so4coop probe - RTTI object scanner (read-only).  Included by probe.c.
 *
 * 1. rtti_init(): finds, in the game module, the MSVC RTTI type descriptors of
 *    the classes below, their CompleteObjectLocators and the vtables that use
 *    them (x64 RTTI: COL = {sig=1, offset, cdOffset, TD rva, CHD rva, self rva};
 *    vtable[-1] = &COL).
 * 2. scan_now() (F9): walks the game's committed read-write memory, finds every
 *    object whose first qword is one of those vtables, logs counts/addresses,
 *    and for each battle character logs which fields point to a "think" (AI)
 *    object or to a battle pad object -> the layout needed for co-op.
 * Nothing is written to game memory.
 */

typedef struct {
    uint64_t BaseAddress, AllocationBase;
    DWORD AllocationProtect, __alignment1;
    uint64_t RegionSize;
    DWORD State, Protect, Type, __alignment2;
} MBI64;
__declspec(dllimport) uint64_t WINAPI VirtualQuery(const void *, MBI64 *, uint64_t);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
#define MEM_COMMIT 0x1000
#define PAGE_GUARD 0x100
#define VK_F9 0x78
#define VK_F10 0x79

/* Hierarchy (from the exe's RTTI): CPlayerEdge : CPlayerThinkChr : CPlayerChr : ...
 * so the AI ("think") is a layer of each character object, not a separate object.
 * Objects carry the vtable of their most derived class, listed here. */
static const char *const CLASSES[] = {
    "CPlayerEdge@Battle", "CPlayerReimi@Battle", "CPlayerFaiz@Battle", "CPlayerLymle@Battle",
    "CPlayerBackus@Battle", "CPlayerArumat@Battle", "CPlayerMelicle@Battle", "CPlayerMuria@Battle",
    "CPlayerSarah@Battle", "CPlayerChr@Battle", "CPlayerThinkChr@Battle", "CEnemyThinkChr@Battle",
    "CBtlPad@Battle",
    "Pad@Aska", "PadXe@Aska", "WindowsPad@Aska", "BasePad@Aska", "PadTask@Peripheral",
    "CTacticsMode@Battle", "CReplaceMember@Battle", "CMemberBoard@Battle", "CBattleMainTask@Battle",
    "CBattleCampMember@Battle", "CTacticsChoice@Battle", "CSelectTarget@Battle",
};
#define NCLASS (sizeof(CLASSES) / sizeof(CLASSES[0]))
#define K_CHR_LAST 11      /* 0..11: battle characters (players + enemies) */
#define K_BTLPAD 12

static uintptr_t self_base, self_end;   /* the probe's own image, skipped by the scan */
static uintptr_t stk_lo, stk_hi;        /* the scanning thread's own stack, skipped too (v0.8):
                                           its locals hold the controlled character during F9 */

static void own_stack(void)
{
    uintptr_t base;
    __asm__ volatile("movq %%gs:0x8, %0" : "=r"(base));        /* TEB.StackBase */
    MBI64 m;
    VirtualQuery((void *)&base, &m, sizeof m);              /* any local: it lives on this stack */
    stk_lo = (uintptr_t)m.AllocationBase;
    stk_hi = base;
}

static int own_memory(uintptr_t a)
{
    return (a >= self_base && a < self_end) || (a >= stk_lo && a < stk_hi);
}

#define MAXVT 64
static struct { uintptr_t vt; uint32_t cls; } vtabs[MAXVT];
static uint32_t nvt;
static int rtti_done;

static int readable(DWORD prot)
{
    if (prot & PAGE_GUARD) return 0;
    prot &= 0xff;
    return prot == 0x02 || prot == 0x04 || prot == 0x08 || prot == 0x20 || prot == 0x40 || prot == 0x80;
}

static int writable(DWORD prot)
{
    if (prot & PAGE_GUARD) return 0;
    prot &= 0xff;
    return prot == 0x04 || prot == 0x08 || prot == 0x40 || prot == 0x80;
}

/* calls fn(start, end) for each readable committed region inside [lo, hi) */
static void each_region(uintptr_t lo, uintptr_t hi, int need_write, void (*fn)(uintptr_t, uintptr_t, void *), void *ctx)
{
    MBI64 m;
    uintptr_t p = lo;
    while (p < hi && VirtualQuery((void *)p, &m, sizeof m)) {
        uintptr_t s = (uintptr_t)m.BaseAddress, e = s + m.RegionSize;
        if (e <= p) break;
        if (m.State == MEM_COMMIT && (need_write ? writable(m.Protect) : readable(m.Protect))) {
            uintptr_t a = s < lo ? lo : s, b = e > hi ? hi : e;
            if (b > a) fn(a, b, ctx);
        }
        p = e;
    }
}

/* ---- step 1: RTTI ---- */
static uint32_t td_rva[NCLASS];

static void find_names(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uint32_t k = 0; k < NCLASS; k++) {
        if (td_rva[k]) continue;
        /* decorated name: ".?AV" + CLASS + "@@" */
        char name[96] = ".?AV";
        uint32_t n = 4;
        for (const char *c = CLASSES[k]; *c; c++) name[n++] = *c;
        name[n++] = '@'; name[n++] = '@'; name[n] = 0;
        for (uintptr_t p = a; p + n + 1 <= b; p++) {
            const char *q = (const char *)p;
            if (q[0] != '.' || q[1] != '?') continue;
            uint32_t i = 0;
            while (i < n && q[i] == name[i]) i++;
            if (i == n && q[n] == 0) { td_rva[k] = (uint32_t)(p - 0x10 - game_base); break; }
        }
    }
}

static uint32_t col_rva[NCLASS][4];

static void find_cols(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uintptr_t p = (a + 3) & ~(uintptr_t)3; p + 24 <= b; p += 4) {
        const uint32_t *c = (const uint32_t *)p;
        if (c[0] != 1 || c[5] != (uint32_t)(p - game_base)) continue;
        for (uint32_t k = 0; k < NCLASS; k++)
            if (td_rva[k] && c[3] == td_rva[k] && c[1] == 0)   /* primary vtable only */
                for (int j = 0; j < 4; j++)
                    if (!col_rva[k][j]) { col_rva[k][j] = (uint32_t)(p - game_base); break; }
    }
}

static void find_vtables(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uintptr_t p = (a + 7) & ~(uintptr_t)7; p + 16 <= b; p += 8) {
        uintptr_t v = *(const uintptr_t *)p;
        if (v < game_base || v >= game_end) continue;
        uint32_t r = (uint32_t)(v - game_base);
        for (uint32_t k = 0; k < NCLASS; k++)
            for (int j = 0; j < 4 && col_rva[k][j]; j++)
                if (r == col_rva[k][j] && nvt < MAXVT) {
                    uintptr_t fn0 = *(const uintptr_t *)(p + 8);   /* a vtable starts with code pointers */
                    if (fn0 >= game_base && fn0 < game_end) { vtabs[nvt].vt = p + 8; vtabs[nvt].cls = k; nvt++; }
                }
    }
}

static void rtti_init(void)
{
    char b[200];
    rtti_done = 1;
    uint64_t t0 = GetTickCount64();
    each_region(game_base, game_end, 0, find_names, 0);
    each_region(game_base, game_end, 0, find_cols, 0);
    each_region(game_base, game_end, 0, find_vtables, 0);
    wsprintfA(b, "RTTI map built in %lu ms, %u vtables:\r\n", (unsigned long)(GetTickCount64() - t0), nvt);
    logs(b);
    for (uint32_t k = 0; k < NCLASS; k++) {
        int found = 0;
        for (uint32_t i = 0; i < nvt; i++)
            if (vtabs[i].cls == k) {
                wsprintfA(b, "  %-24s TD=+0x%08lx vtable=game+0x%08lx\r\n", CLASSES[k], (unsigned long)td_rva[k],
                          (unsigned long)(vtabs[i].vt - game_base));
                logs(b); found = 1;
            }
        if (!found) {
            wsprintfA(b, "  %-24s TD=+0x%08lx (no vtable found)\r\n", CLASSES[k], (unsigned long)td_rva[k]);
            logs(b);
        }
    }
}

/* ---- step 2: instances ---- */
#define MAXOBJ 256
static struct { uintptr_t addr; uint32_t cls; } objs[MAXOBJ];
static uint32_t nobj, cls_count[NCLASS];

static int vt_class(uintptr_t v)
{
    if (v < game_base || v >= game_end) return -1;
    for (uint32_t i = 0; i < nvt; i++) if (vtabs[i].vt == v) return (int)vtabs[i].cls;
    return -1;
}

static void find_objs(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    if (a >= game_base && a < game_end) return;      /* skip the module's own data (vtables live there) */
    if (own_memory(a)) return;                       /* skip the probe itself (its tables and stack) */
    for (uintptr_t p = (a + 7) & ~(uintptr_t)7; p + 8 <= b; p += 8) {
        int k = vt_class(*(const uintptr_t *)p);
        if (k < 0) continue;
        cls_count[k]++;
        if (nobj < MAXOBJ) { objs[nobj].addr = p; objs[nobj].cls = (uint32_t)k; nobj++; }
    }
}

static int obj_class_at(uintptr_t a)
{
    for (uint32_t i = 0; i < nobj; i++) if (objs[i].addr == a) return (int)objs[i].cls;
    return -1;
}

static uint32_t scan_no;
static void dump_objects(void);
static void dump_one(const char *tag, uintptr_t o, uintptr_t size);
static void dump_path(uint16_t *path, const char *nm);
static void dump_globals(void);
static void battle_dump(void);

static int mem_readable(uintptr_t p, uintptr_t n)
{
    MBI64 m;
    if (p < 0x10000 || p > (uintptr_t)0x7fffffff0000ULL) return 0;
    if (!VirtualQuery((void *)p, &m, sizeof m)) return 0;
    if (m.State != MEM_COMMIT || !readable(m.Protect)) return 0;
    return p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}

/* MSVC x64 RTTI: *obj = vtable, vtable[-1] = COL{sig=1,...,TD rva @12,..., self rva @20} */
static const char *class_of(uintptr_t obj)
{
    if (!mem_readable(obj, 8)) return 0;
    uintptr_t vt = *(const uintptr_t *)obj;
    if (vt < game_base + 8 || vt >= game_end) return 0;
    uintptr_t col = *(const uintptr_t *)(vt - 8);
    if (col < game_base || col + 24 > game_end) return 0;
    const uint32_t *c = (const uint32_t *)col;
    if (c[0] != 1 || c[5] != (uint32_t)(col - game_base)) return 0;
    uintptr_t td = game_base + c[3];
    if (td + 0x14 >= game_end) return 0;
    const char *name = (const char *)(td + 0x10);
    return (name[0] == '.' && name[1] == '?') ? name + 4 : 0;   /* skip ".?AV" */
}

/* logs the class of every pointer field of an object, one level deep */
static void describe_fields(const char *tag, uintptr_t o, uintptr_t span)
{
    char b[256];
    const char *cn = class_of(o);
    wsprintfA(b, "  %s @%p class=%s\r\n", tag, (void *)o, cn ? cn : "?");
    logs(b);
    if (!mem_readable(o, span)) return;
    for (uintptr_t f = 0; f < span; f += 8) {
        uintptr_t v = *(const uintptr_t *)(o + f);
        const char *k = class_of(v);
        if (k) { wsprintfA(b, "     +0x%04lx -> %p %s\r\n", (unsigned long)f, (void *)v, k); logs(b); }
    }
}

/* class_of() without VirtualQuery for the object itself (caller knows it is readable) */
static const char *class_of_fast(uintptr_t obj)
{
    uintptr_t vt = *(const uintptr_t *)obj;
    if (vt < game_base + 8 || vt >= game_end) return 0;
    uintptr_t col = *(const uintptr_t *)(vt - 8);
    if (col < game_base || col + 24 > game_end) return 0;
    const uint32_t *c = (const uint32_t *)col;
    if (c[0] != 1 || c[5] != (uint32_t)(col - game_base)) return 0;
    uintptr_t td = game_base + c[3];
    if (td + 0x14 >= game_end) return 0;
    const char *name = (const char *)(td + 0x10);
    return (name[0] == '.' && name[1] == '?') ? name + 4 : 0;
}

/* ---- reference search: every qword in writable memory that points to a party character ---- */
#define MAXCHR 8
static uintptr_t ref_chr[MAXCHR];
static const char *ref_name[MAXCHR];
static uint32_t nref_chr;
static HANDLE ref_file;
static uint32_t ref_hits;

/* exact (+0) references of the current and previous scan, kept for watch.c */
typedef struct { uintptr_t slot, target, base; const char *cls; } ref_t;   /* cls 0: global or heap without RTTI */
#define MAXREF 4096
static ref_t refs_cur[MAXREF], refs_prev[MAXREF];
static uint32_t nref_cur, nref_prev;
static uintptr_t controlled_cur, controlled_prev;   /* character with the camera block (+0x129a0) */
static void watch_after_scan(void);
static void watch_report(void);

static void ref_line(uintptr_t loc, uintptr_t region_lo, uint32_t ci, uintptr_t delta)
{
    char b[256], where[160];
    const char *cls = 0;
    uintptr_t base = 0;
    if (loc >= game_base && loc < game_end)
        wsprintfA(where, "game+0x%08lx", (unsigned long)(loc - game_base));
    else {
        where[0] = 0;
        for (uintptr_t q = loc & ~(uintptr_t)7; q >= region_lo && loc - q < 0x4000; q -= 8) {
            const char *cn = class_of_fast(q);
            if (cn) { cls = cn; base = q; wsprintfA(where, "%p in %s@%p+0x%lx", (void *)loc, cn, (void *)q, (unsigned long)(loc - q)); break; }
            if (q < 8) break;
        }
        if (!where[0]) wsprintfA(where, "%p (heap)", (void *)loc);
    }
    if (delta == 0 && nref_cur < MAXREF) {
        ref_t *r = &refs_cur[nref_cur++];
        r->slot = loc; r->target = ref_chr[ci]; r->base = base; r->cls = cls;
    }
    wsprintfA(b, "%s -> %s+0x%lx\r\n", where, ref_name[ci], (unsigned long)delta);
    DWORD n = 0, w;
    while (b[n]) n++;
    WriteFile(ref_file, b, n, &w, 0);
}

static void find_refs(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    if (own_memory(a)) return;
    for (uintptr_t p = (a + 7) & ~(uintptr_t)7; p + 8 <= b; p += 8) {
        uintptr_t v = *(const uintptr_t *)p;
        for (uint32_t i = 0; i < nref_chr; i++) {
            uintptr_t d = v - ref_chr[i];
            if (d == 0 || d == 0x770 || d == 0x778) {
                if (p >= ref_chr[i] && p < ref_chr[i] + 0x40000) break;   /* the character's own fields */
                if (ref_hits++ < 20000) ref_line(p, a, i, d);
                break;
            }
        }
    }
}

static void search_refs(void)
{
    nref_chr = 0;
    nref_cur = 0;
    for (uint32_t i = 0; i < nobj && nref_chr < MAXCHR; i++)
        if (objs[i].cls <= 8) { ref_chr[nref_chr] = objs[i].addr; ref_name[nref_chr] = CLASSES[objs[i].cls]; nref_chr++; }
    if (!nref_chr) return;
    char nm[64];
    wsprintfA(nm, "\\s%02u_refs.txt", scan_no);
    uint16_t path[700];
    dump_path(path, nm);
    ref_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (ref_file == (HANDLE)-1) return;
    ref_hits = 0;
    uint64_t t0 = GetTickCount64();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_refs, 0);   /* includes the game's globals */
    CloseHandle(ref_file);
    char b[128];
    wsprintfA(b, "  references to party characters: %u (%lu ms) -> s%02u_refs.txt\r\n", ref_hits,
              (unsigned long)(GetTickCount64() - t0), scan_no);
    logs(b);
}

static void analyse_operator(void)
{
    char b[200];
    for (uint32_t i = 0; i < nobj; i++) {
        uint32_t k = objs[i].cls;
        uintptr_t o = objs[i].addr;
        if (k == 21 /* CBattleMainTask */ && mem_readable(o, 0x420)) {
            uintptr_t x = *(const uintptr_t *)(o + 0x410);
            wsprintfA(b, "  CBattleMainTask@%p +0x410 (operator X) = %p\r\n", (void *)o, (void *)x); logs(b);
            if (x) { describe_fields("X", x, 0x800); dump_one("X", x, 0x4000); }
        }
        if (k <= 8 /* player characters */ && mem_readable(o + 0x12990, 0xb0)) {
            uintptr_t x = *(const uintptr_t *)(o + 0x129a0), y = *(const uintptr_t *)(o + 0x129c0);
            if (!x) continue;
            controlled_cur = o;
            wsprintfA(b, "  CONTROLLED: %s@%p  +0x129a0=%p  +0x129c0=%p  +0x129d8=%08lx\r\n", CLASSES[k], (void *)o,
                      (void *)x, (void *)y, (unsigned long)*(const uint32_t *)(o + 0x129d8));
            logs(b);
            for (uintptr_t f = 0x12990; f < 0x12a40; f += 8) {
                uintptr_t v = *(const uintptr_t *)(o + f);
                const char *cn = class_of(v);
                if (cn) { wsprintfA(b, "     chr+0x%05lx -> %p %s\r\n", (unsigned long)f, (void *)v, cn); logs(b); }
            }
            if (y) { describe_fields("Y", y, 0x800); dump_one("Y", y, 0x4000); }
        }
    }
}

static void scan_now(void)
{
    char b[200];
    if (!rtti_done) rtti_init();
    nobj = 0;
    controlled_cur = 0;
    own_stack();
    for (uint32_t k = 0; k < NCLASS; k++) cls_count[k] = 0;
    uint64_t t0 = GetTickCount64();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, find_objs, 0);
    wsprintfA(b, "=== SCAN #%u (%lu ms) === (probe stack %p-%p skipped)\r\n", ++scan_no, (unsigned long)(GetTickCount64() - t0),
              (void *)stk_lo, (void *)stk_hi);
    logs(b);
    for (uint32_t k = 0; k < NCLASS; k++)
        if (cls_count[k]) {
            wsprintfA(b, "  %-24s x%u:", CLASSES[k], cls_count[k]); logs(b);
            uint32_t shown = 0;
            for (uint32_t i = 0; i < nobj && shown < 6; i++)
                if (objs[i].cls == k) { wsprintfA(b, " %p", (void *)objs[i].addr); logs(b); shown++; }
            logs("\r\n");
        }
    /* links: fields (pointers) of every found object that point to another found object */
    for (uint32_t i = 0; i < nobj; i++) {
        uintptr_t o = objs[i].addr;
        MBI64 m;
        if (!VirtualQuery((void *)o, &m, sizeof m)) continue;
        uintptr_t lim = (uintptr_t)m.BaseAddress + m.RegionSize;
        uintptr_t span = objs[i].cls <= K_CHR_LAST ? 0x4000 : 0x1000;
        int any = 0;
        for (uintptr_t f = 8; f < span && o + f + 8 <= lim; f += 8) {
            uintptr_t v = *(const uintptr_t *)(o + f);
            int k = obj_class_at(v);
            if (k < 0) continue;
            if (!any) { wsprintfA(b, "  %s@%p ->", CLASSES[objs[i].cls], (void *)o); logs(b); any = 1; }
            wsprintfA(b, " +0x%lx=%s@%p", (unsigned long)f, CLASSES[k], (void *)v); logs(b);
        }
        if (any) logs("\r\n");
    }
    analyse_operator();
    search_refs();
    dump_objects();
    battle_dump();
    dump_globals();
    watch_after_scan();
}

/* the game's writable, non-executable sections (its global variables), for offline diffing:
 * a party index ("member 1 is controlled") would not show up in the pointer search */
static BYTE zero_page[0x1000];
static void dump_globals(void)
{
    BYTE *b = (BYTE *)game_base;
    int32_t pe = *(int32_t *)(b + 0x3c);
    uint16_t nsec = *(uint16_t *)(b + pe + 6), optsz = *(uint16_t *)(b + pe + 20);
    BYTE *sec = b + pe + 24 + optsz;
    for (uint16_t i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = *(uint32_t *)(sec + 8), rva = *(uint32_t *)(sec + 12), fl = *(uint32_t *)(sec + 36);
        if (!(fl & 0x80000000) || (fl & 0x20000000) || vsize > 0x2000000) continue;   /* writable, not code, < 32 MB */
        char nm[64];
        wsprintfA(nm, "\\s%02u_globals_%08lx.bin", scan_no, (unsigned long)rva);
        uint16_t path[700];
        dump_path(path, nm);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == (HANDLE)-1) continue;
        uint32_t zeros = 0;
        for (uint32_t off = 0; off < vsize; off += 0x1000) {
            uint32_t n = vsize - off < 0x1000 ? vsize - off : 0x1000;
            uintptr_t p = game_base + rva + off;
            int ok = mem_readable(p, n);
            if (!ok) zeros++;
            DWORD w;
            WriteFile(h, ok ? (const void *)p : zero_page, n, &w, 0);
        }
        CloseHandle(h);
        char line[160];
        wsprintfA(line, "  globals: rva 0x%08lx size 0x%08lx -> s%02u_globals_%08lx.bin (%lu unreadable pages)\r\n",
                  (unsigned long)rva, (unsigned long)vsize, scan_no, (unsigned long)rva, (unsigned long)zeros);
        logs(line);
    }
}

static void dump_path(uint16_t *path, const char *nm)
{
    DWORD n = GetModuleFileNameW(self_module, path, 512);
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    path[n] = 0;
    wcat(path, "so4coop_dumps");
    CreateDirectoryW(path, 0);
    wcat(path, nm);
}

static void dump_one(const char *tag, uintptr_t o, uintptr_t size)
{
    MBI64 m;
    if (!VirtualQuery((void *)o, &m, sizeof m)) return;
    uintptr_t lim = (uintptr_t)m.BaseAddress + m.RegionSize;
    if (o + size > lim) size = lim - o;
    char nm[96];
    wsprintfA(nm, "\\s%02u_%s_%p.bin", scan_no, tag, (void *)o);
    uint16_t path[700];
    dump_path(path, nm);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == (HANDLE)-1) return;
    DWORD w;
    WriteFile(h, (const void *)o, (DWORD)size, &w, 0);
    CloseHandle(h);
}

/* raw dumps for offline diffing: so4coop_dumps/s<scan>_<class>_<addr>.bin */
static void dump_objects(void)
{
    uint16_t dir[520];
    DWORD n = GetModuleFileNameW(self_module, dir, 512);
    while (n && dir[n - 1] != '\\' && dir[n - 1] != '/') n--;
    dir[n] = 0;
    wcat(dir, "so4coop_dumps");
    CreateDirectoryW(dir, 0);
    uint32_t files = 0;
    for (uint32_t i = 0; i < nobj && files < 64; i++) {
        uint32_t k = objs[i].cls;
        uintptr_t size = k <= K_CHR_LAST ? 0x40000 : 0x2000;   /* characters are big objects */
        uintptr_t o = objs[i].addr;
        MBI64 m;
        if (!VirtualQuery((void *)o, &m, sizeof m)) continue;
        uintptr_t lim = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (o + size > lim) size = lim - o;
        char nm[128];
        wsprintfA(nm, "\\s%02u_%s_%p.bin", scan_no, CLASSES[k], (void *)o);
        for (char *c = nm; *c; c++) if (*c == '@') *c = '-';
        uint16_t path[700];
        uint32_t j = 0;
        while (dir[j]) { path[j] = dir[j]; j++; }
        path[j] = 0;
        wcat(path, nm);
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (h == (HANDLE)-1) continue;
        DWORD w;
        WriteFile(h, (const void *)o, (DWORD)size, &w, 0);
        CloseHandle(h);
        files++;
    }
    char b[96];
    wsprintfA(b, "  %u objects dumped to so4coop_dumps\\\r\n", files);
    logs(b);
}

static int f6_was, f7_was, f8_was, f9_was, f10_was, f11_was;
static uint32_t marks;
#define VK_F11 0x7a
static void write_test(void);
static void autocap_toggle(void);
static void stick_capture(void);
static void guard_trace(void);
static int f5_was, f3_was, f4_was;
static void tt_take(int player);

static void poll_hotkeys(void)
{
    int f3 = (GetAsyncKeyState(0x72 /* VK_F3 */) & 0x8000) != 0;
    int f4 = (GetAsyncKeyState(0x73 /* VK_F4 */) & 0x8000) != 0;
    if (f3 && !f3_was) { logs("=== TAG-TEAM: F3 (player 1) ===\r\n"); tt_take(0); }
    if (f4 && !f4_was) { logs("=== TAG-TEAM: F4 (player 2) ===\r\n"); tt_take(1); }
    f3_was = f3; f4_was = f4;
#ifdef SO4COOP_RELEASE
    return;                      /* release build: only the tag-team keys */
#endif
    int f5 = (GetAsyncKeyState(0x74 /* VK_F5 */) & 0x8000) != 0;
    if (f5 && !f5_was) guard_trace();
    f5_was = f5;
    int f6 = (GetAsyncKeyState(0x75 /* VK_F6 */) & 0x8000) != 0;
    if (f6 && !f6_was) stick_capture();
    f6_was = f6;
    int f7 = (GetAsyncKeyState(0x76 /* VK_F7 */) & 0x8000) != 0;
    if (f7 && !f7_was) autocap_toggle();
    f7_was = f7;
    int f8 = (GetAsyncKeyState(0x77 /* VK_F8 */) & 0x8000) != 0;
    int f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    int f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    int f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f8 && !f8_was) write_test();
    if (f9 && !f9_was) scan_now();
    if (f10 && !f10_was) { char b[64]; wsprintfA(b, "----- MARK #%u -----\r\n", ++marks); logs(b); }
    if (f11 && !f11_was) watch_report();
    f8_was = f8; f9_was = f9; f10_was = f10; f11_was = f11;
}
