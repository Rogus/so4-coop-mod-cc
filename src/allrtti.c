/*
 * so4coop probe - every Battle:: class (v0.12).  Included by probe.c after watch.c.
 *
 * Five rounds of pointer searches found only camera objects following the controlled
 * character, while UI classes (CArrowCursorManager, CMinimapPlayerMarkerElement,
 * CMemberBoardChangeArrowElement) must know who is controlled: it is likely stored as an
 * index, in an object the 24-class map does not know. So:
 *   ball_init(): RTTI map of all classes named ".?AV...@Battle@@" (type descriptors ->
 *                primary complete-object locators -> vtables), kept sorted;
 *   battle_dump() (each F9): every instance of those classes (capped per class), first 4 KB,
 *                into s<NN>_battle_objects.bin + s<NN>_battle_index.txt for offline diffing.
 * Read-only.
 */

#define MAXBC 1500
static struct { uint32_t td; const char *name; } bcls[MAXBC];
static uint32_t nbcls;
static struct { uint32_t rva; uint32_t cls; } bcol[MAXBC * 2];
static uint32_t nbcol;
static struct { uintptr_t vt; uint32_t cls; } bvt[MAXBC * 2];
static uint32_t nbvt;
static uintptr_t bvt_lo = ~(uintptr_t)0, bvt_hi;
static int ball_done;

static int ends_with(const char *s, uint32_t n, const char *suf)
{
    uint32_t m = 0;
    while (suf[m]) m++;
    if (n < m) return 0;
    for (uint32_t i = 0; i < m; i++) if (s[n - m + i] != suf[i]) return 0;
    return 1;
}

static void ball_names(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uintptr_t p = a + 0x10; p + 16 <= b && nbcls < MAXBC; p++) {
        const char *q = (const char *)p;
        if (q[0] != '.' || q[1] != '?' || q[2] != 'A' || (q[3] != 'V' && q[3] != 'U')) continue;
        uint32_t n = 4;
        while (n < 200 && p + n < b && q[n]) n++;
        if (n >= 200 || p + n >= b || !ends_with(q, n, "@Battle@@")) continue;
        uintptr_t tdvt = *(const uintptr_t *)(p - 0x10);          /* TypeDescriptor.pVFTable = type_info vtable */
        if (tdvt < game_base || tdvt >= game_end) continue;
        bcls[nbcls].td = (uint32_t)(p - 0x10 - game_base);
        bcls[nbcls].name = q + 4;
        nbcls++;
        p += n;
    }
}

static int bcls_by_td(uint32_t td)
{
    uint32_t lo = 0, hi = nbcls;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (bcls[m].td == td) return (int)m;
        if (bcls[m].td < td) lo = m + 1; else hi = m;
    }
    return -1;
}

static void ball_cols(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uintptr_t p = (a + 3) & ~(uintptr_t)3; p + 24 <= b && nbcol < MAXBC * 2; p += 4) {
        const uint32_t *c = (const uint32_t *)p;
        if (c[0] != 1 || c[1] != 0 || c[5] != (uint32_t)(p - game_base)) continue;   /* primary vtable only */
        int k = bcls_by_td(c[3]);
        if (k < 0) continue;
        bcol[nbcol].rva = (uint32_t)(p - game_base);
        bcol[nbcol].cls = (uint32_t)k;
        nbcol++;
    }
}

static int bcol_find(uint32_t rva)
{
    uint32_t lo = 0, hi = nbcol;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (bcol[m].rva == rva) return (int)bcol[m].cls;
        if (bcol[m].rva < rva) lo = m + 1; else hi = m;
    }
    return -1;
}

static void ball_vtables(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    for (uintptr_t p = (a + 7) & ~(uintptr_t)7; p + 16 <= b && nbvt < MAXBC * 2; p += 8) {
        uintptr_t v = *(const uintptr_t *)p;
        if (v < game_base || v >= game_end) continue;
        int k = bcol_find((uint32_t)(v - game_base));
        if (k < 0) continue;
        uintptr_t fn0 = *(const uintptr_t *)(p + 8);
        if (fn0 < game_base || fn0 >= game_end) continue;
        bvt[nbvt].vt = p + 8;
        bvt[nbvt].cls = (uint32_t)k;
        if (p + 8 < bvt_lo) bvt_lo = p + 8;
        if (p + 8 > bvt_hi) bvt_hi = p + 8;
        nbvt++;
    }
}

static int bvt_find(uintptr_t v)
{
    uint32_t lo = 0, hi = nbvt;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (bvt[m].vt == v) return (int)bvt[m].cls;
        if (bvt[m].vt < v) lo = m + 1; else hi = m;
    }
    return -1;
}

static void ball_init(void)
{
    char b[200];
    ball_done = 1;
    uint64_t t0 = GetTickCount64();
    each_region(game_base, game_end, 0, ball_names, 0);
    for (uint32_t i = 1; i < nbcls; i++)                        /* sort by TD rva */
        for (uint32_t j = i; j > 0 && bcls[j].td < bcls[j - 1].td; j--) {
            uint32_t t = bcls[j].td; const char *n = bcls[j].name;
            bcls[j].td = bcls[j - 1].td; bcls[j].name = bcls[j - 1].name;
            bcls[j - 1].td = t; bcls[j - 1].name = n;
        }
    each_region(game_base, game_end, 0, ball_cols, 0);
    for (uint32_t i = 1; i < nbcol; i++)
        for (uint32_t j = i; j > 0 && bcol[j].rva < bcol[j - 1].rva; j--) {
            uint32_t r = bcol[j].rva, c = bcol[j].cls;
            bcol[j].rva = bcol[j - 1].rva; bcol[j].cls = bcol[j - 1].cls;
            bcol[j - 1].rva = r; bcol[j - 1].cls = c;
        }
    each_region(game_base, game_end, 0, ball_vtables, 0);
    for (uint32_t i = 1; i < nbvt; i++)
        for (uint32_t j = i; j > 0 && bvt[j].vt < bvt[j - 1].vt; j--) {
            uintptr_t v = bvt[j].vt; uint32_t c = bvt[j].cls;
            bvt[j].vt = bvt[j - 1].vt; bvt[j].cls = bvt[j - 1].cls;
            bvt[j - 1].vt = v; bvt[j - 1].cls = c;
        }
    wsprintfA(b, "all Battle classes: %lu type descriptors, %lu locators, %lu vtables (%lu ms)\r\n", (unsigned long)nbcls,
              (unsigned long)nbcol, (unsigned long)nbvt, (unsigned long)(GetTickCount64() - t0));
    logs(b);
}

#define MAXBOBJ 12000
#define BCAP 48                 /* instances per class */
#define BSIZE 0x1000
static struct { uintptr_t addr; uint32_t cls; } bobj[MAXBOBJ];
static uint32_t nbobj, bobj_over;
static uint32_t bcount[MAXBC];

static void ball_objs(uintptr_t a, uintptr_t b, void *ctx)
{
    (void)ctx;
    if (a >= game_base && a < game_end) return;
    if (own_memory(a)) return;
    for (uintptr_t p = (a + 7) & ~(uintptr_t)7; p + 8 <= b; p += 8) {
        uintptr_t v = *(const uintptr_t *)p;
        if (v < bvt_lo || v > bvt_hi) continue;
        int k = bvt_find(v);
        if (k < 0) continue;
        if (++bcount[k] <= BCAP && nbobj < MAXBOBJ) { bobj[nbobj].addr = p; bobj[nbobj].cls = (uint32_t)k; nbobj++; }
        else bobj_over++;
    }
}

static void battle_dump(void)
{
    char b[300], nm[64];
    if (!ball_done) ball_init();
    if (!nbvt) return;
    nbobj = bobj_over = 0;
    for (uint32_t k = 0; k < nbcls; k++) bcount[k] = 0;
    uint64_t t0 = GetTickCount64();
    each_region(0x10000, (uintptr_t)0x7fffffff0000ULL, 1, ball_objs, 0);
    uint16_t path[700];
    wsprintfA(nm, "\\s%02u_battle_objects.bin", scan_no);
    dump_path(path, nm);
    HANDLE hb = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    wsprintfA(nm, "\\s%02u_battle_index.txt", scan_no);
    dump_path(path, nm);
    HANDLE hi = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (hb == (HANDLE)-1 || hi == (HANDLE)-1) {
        if (hb != (HANDLE)-1) CloseHandle(hb);
        if (hi != (HANDLE)-1) CloseHandle(hi);
        logs("  battle objects: cannot create files\r\n");
        return;
    }
    uint64_t off = 0;
    DWORD w, n;
    for (uint32_t i = 0; i < nbobj; i++) {
        MBI64 m;
        uintptr_t o = bobj[i].addr, size = BSIZE;
        if (!VirtualQuery((void *)o, &m, sizeof m)) continue;
        uintptr_t lim = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (o + size > lim) size = lim - o;
        WriteFile(hb, (const void *)o, (DWORD)size, &w, 0);
        wsprintfA(b, "%s %p %lu %lu\r\n", bcls[bobj[i].cls].name, (void *)o, (unsigned long)off, (unsigned long)size);
        n = 0; while (b[n]) n++;
        WriteFile(hi, b, n, &w, 0);
        off += size;
    }
    CloseHandle(hb);
    CloseHandle(hi);
    wsprintfA(b, "  battle objects: %lu instances (%lu over the cap) of %lu classes, %lu KB -> s%02u_battle_objects.bin (%lu ms)\r\n",
              (unsigned long)nbobj, (unsigned long)bobj_over, (unsigned long)nbcls, (unsigned long)(off / 1024), scan_no,
              (unsigned long)(GetTickCount64() - t0));
    logs(b);
}
