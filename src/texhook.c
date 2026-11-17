/*
 * so4coop - button prompt research (v0.40): log and save the textures the game creates.  Included after
 * displayfix.c; research build only.
 *
 * Goal: PlayStation button icons without Special K. The game data is in 0000.bin (encrypted) and 0001.bin
 * (KCAP packs, SLZ-compressed), so the icons are replaced at runtime instead: the game renders with
 * Direct3D 11 (DXVK under Proton), and every device of a D3D11 implementation shares one vtable. A
 * short-lived device of our own gives that vtable; its CreateTexture2D slot (5) is redirected, so every
 * texture the game creates afterwards passes through tx_create().
 * This round only observes: each texture created with data is listed (so4coop_textures/textures.txt:
 * hash, size, format, flags) and each new one in an image format usable for UI icons is saved once as a
 * .dds (top mip level) for offline viewing. Nothing the game receives is changed.
 */

static void wcopy(uint16_t *d, const uint16_t *s);

typedef struct { uint32_t Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags; } TX_DESC;
typedef struct { const void *pSysMem; uint32_t SysMemPitch, SysMemSlicePitch; } TX_INIT;
typedef long (*tx_create_fn)(void *self, const TX_DESC *desc, const TX_INIT *init, void **out);
typedef long (*tx_d3d11createdevice_fn)(void *adapter, int driver, HMODULE sw, uint32_t flags, const int *levels, uint32_t nlevels,
                                        uint32_t sdk, void **dev, int *level, void **ctx);

static tx_create_fn tx_orig;
static void **tx_vtable;
static volatile int tx_installed, tx_tried;
static volatile long tx_lock;
static HANDLE tx_list = (HANDLE)-1;
static uint16_t tx_dir[520];
static volatile long tx_n_data, tx_n_nodata, tx_n_saved, tx_n_dupes;
static volatile uint64_t tx_bytes_saved;
static uint64_t tx_t0, tx_report_t;
#define TX_SEEN 65536
static uint64_t tx_seen[TX_SEEN];
#define TX_MAX_FILES 6000
#define TX_MAX_BYTES (3ULL << 30)

static void tx_acquire(void) { while (__sync_lock_test_and_set(&tx_lock, 1)) Sleep(0); }
static void tx_release(void) { __sync_lock_release(&tx_lock); }

/* bytes per 4x4 block (block formats) or per pixel; 0 = unknown */
static uint32_t tx_fmt(uint32_t f, int *block)
{
    *block = 1;
    switch (f) {
    case 70: case 71: case 72: case 79: case 80: case 81: return 8;                       /* BC1, BC4 */
    case 73: case 74: case 75: case 76: case 77: case 78: case 82: case 83: case 84:       /* BC2, BC3, BC5 */
    case 94: case 95: case 96: case 97: case 98: case 99: return 16;                      /* BC6H, BC7 */
    }
    *block = 0;
    switch (f) {
    case 2: case 3: case 4: return 16;                                                      /* R32G32B32A32 */
    case 10: case 11: case 12: case 13: case 14: return 8;                                  /* R16G16B16A16 */
    case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32:
    case 41: case 87: case 88: case 90: case 91: case 92: case 93: return 4;
    case 48: case 49: case 50: case 51: case 52: case 54: case 56: case 57: case 85: case 86: case 115: return 2;
    case 60: case 61: case 62: case 63: case 64: case 65: return 1;
    }
    return 0;
}

/* the formats a UI icon sheet can be in: saved as .dds */
static int tx_saveable(uint32_t f)
{
    switch (f) {
    case 27: case 28: case 29: case 87: case 91: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 78:
    case 97: case 98: case 99: return 1;              /* v0.41: + the TYPELESS formats the game uses (27, 70, 73, 76, 97) */
    }
    return 0;
}

static void tx_hexq(char *o, uint64_t v) { for (int i = 15; i >= 0; i--) { o[i] = "0123456789abcdef"[v & 15]; v >>= 4; } o[16] = 0; }

/* DDS: legacy headers where common readers need them (DXT1/3/5, 32-bit RGBA/BGRA), DX10 for BC7 */
static void tx_save(uint64_t h, const TX_DESC *d, const BYTE *p, uint32_t pitch, uint32_t rowbytes, uint32_t rows)
{
    uint16_t path[600];
    char name[80], hx[17];
    tx_hexq(hx, h);
    wsprintfA(name, "\\%s_%lux%lu_f%lu.dds", hx, (unsigned long)d->Width, (unsigned long)d->Height, (unsigned long)d->Format);
    wcopy(path, tx_dir);
    wcat(path, name);
    HANDLE f = CreateFileW(path, 0x40000000 /* GENERIC_WRITE */, 0, 0, 2 /* CREATE_ALWAYS */, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == (HANDLE)-1) return;
    uint32_t hd[32 + 5];
    memset(hd, 0, sizeof hd);
    hd[0] = 0x20534444;                                  /* "DDS " */
    hd[1] = 124;
    int block;
    tx_fmt(d->Format, &block);
    hd[2] = 0x1 | 0x2 | 0x4 | 0x1000 | (block ? 0x80000 : 0x8);
    hd[3] = d->Height; hd[4] = d->Width;
    hd[5] = block ? rowbytes * rows : rowbytes;
    hd[7] = 1;                                           /* mip count */
    hd[19] = 32;                                         /* pixel format size */
    uint32_t f2 = d->Format, n = 32;
    if (f2 == 70 || f2 == 73 || f2 == 76 || f2 == 27 || f2 == 97) f2++;     /* TYPELESS -> UNORM for the file */
    if (f2 == 71 || f2 == 72) { hd[20] = 0x4; hd[21] = 0x31545844; }        /* DXT1 */
    else if (f2 == 74 || f2 == 75) { hd[20] = 0x4; hd[21] = 0x33545844; }   /* DXT3 */
    else if (f2 == 77 || f2 == 78) { hd[20] = 0x4; hd[21] = 0x35545844; }   /* DXT5 */
    else if (f2 == 28 || f2 == 29) { hd[20] = 0x41; hd[22] = 32; hd[23] = 0xff; hd[24] = 0xff00; hd[25] = 0xff0000; hd[26] = 0xff000000u; }
    else if (f2 == 87 || f2 == 91) { hd[20] = 0x41; hd[22] = 32; hd[23] = 0xff0000; hd[24] = 0xff00; hd[25] = 0xff; hd[26] = 0xff000000u; }
    else { hd[20] = 0x4; hd[21] = 0x30315844; hd[32] = f2; hd[33] = 3; hd[35] = 1; n = 37; }  /* DX10 */
    hd[27] = 0x1000;                                     /* caps: texture */
    DWORD w;
    WriteFile(f, hd, n * 4, &w, 0);
    for (uint32_t r = 0; r < rows; r++) WriteFile(f, p + (uint64_t)r * pitch, rowbytes, &w, 0);
    CloseHandle(f);
    __sync_fetch_and_add(&tx_n_saved, 1);
    __sync_fetch_and_add(&tx_bytes_saved, (uint64_t)rowbytes * rows);
}

static long tx_create(void *self, const TX_DESC *desc, const TX_INIT *init, void **out)
{
    long hr = tx_orig(self, desc, init, out);
    if (hr < 0 || !desc) return hr;
    if (!init || !init->pSysMem) {                        /* v0.41: listed too (data arrives later) */
        __sync_fetch_and_add(&tx_n_nodata, 1);
        char line[200];
        wsprintfA(line, "%lu\t(no data)\t%lux%lu\tmips %lu\tarray %lu\tfmt %lu\tbind %lx\tusage %lu\tmisc %lx\tthread %lu\tcpu %lx\r\n",
                  (unsigned long)(GetTickCount64() - tx_t0), (unsigned long)desc->Width, (unsigned long)desc->Height, (unsigned long)desc->MipLevels,
                  (unsigned long)desc->ArraySize, (unsigned long)desc->Format, (unsigned long)desc->BindFlags, (unsigned long)desc->Usage,
                  (unsigned long)desc->MiscFlags, (unsigned long)GetCurrentThreadId(), (unsigned long)desc->CPUAccessFlags);
        DWORD n = 0, w;
        while (line[n]) n++;
        tx_acquire();
        if (tx_list != (HANDLE)-1) WriteFile(tx_list, line, n, &w, 0);
        tx_release();
        return hr;
    }
    __sync_fetch_and_add(&tx_n_data, 1);
    int block;
    uint32_t bpe = tx_fmt(desc->Format, &block);
    uint32_t rowbytes = 0, rows = 0;
    uint64_t h = 1469598103934665603ULL;                 /* FNV-1a 64 over size, format and the top mip level */
    h = (h ^ desc->Width) * 1099511628211ULL; h = (h ^ desc->Height) * 1099511628211ULL; h = (h ^ desc->Format) * 1099511628211ULL;
    if (bpe) {
        rowbytes = block ? ((desc->Width + 3) / 4) * bpe : desc->Width * bpe;
        rows = block ? (desc->Height + 3) / 4 : desc->Height;
        if (init->SysMemPitch >= rowbytes) {
            const BYTE *p = (const BYTE *)init->pSysMem;
            for (uint32_t r = 0; r < rows; r++) {
                const uint64_t *q = (const uint64_t *)(p + (uint64_t)r * init->SysMemPitch);
                uint32_t k = 0;
                for (; k + 8 <= rowbytes; k += 8) h = (h ^ q[k / 8]) * 1099511628211ULL;
                for (; k < rowbytes; k++) h = (h ^ p[(uint64_t)r * init->SysMemPitch + k]) * 1099511628211ULL;
            }
        } else rowbytes = 0;
    }
    if (!h) h = 1;
    int fresh = 0;
    tx_acquire();
    for (uint32_t i = (uint32_t)(h ^ (h >> 29)) & (TX_SEEN - 1), n = 0; n < TX_SEEN; i = (i + 1) & (TX_SEEN - 1), n++) {
        if (tx_seen[i] == h) break;
        if (!tx_seen[i]) { tx_seen[i] = h; fresh = 1; break; }
    }
    tx_release();
    if (!fresh) { __sync_fetch_and_add(&tx_n_dupes, 1); return hr; }
    int save = rowbytes && tx_saveable(desc->Format) && desc->ArraySize == 1 && desc->Width >= 16 && desc->Height >= 16 &&
               desc->Width <= 8192 && desc->Height <= 8192 && tx_n_saved < TX_MAX_FILES && tx_bytes_saved < TX_MAX_BYTES;
    if (save) tx_save(h, desc, (const BYTE *)init->pSysMem, init->SysMemPitch, rowbytes, rows);
    char line[200], hx[17];
    tx_hexq(hx, h);
    wsprintfA(line, "%lu\t%s\t%lux%lu\tmips %lu\tarray %lu\tfmt %lu\tbind %lx\tusage %lu\tmisc %lx\tthread %lu\t%s\r\n",
              (unsigned long)(GetTickCount64() - tx_t0), hx, (unsigned long)desc->Width, (unsigned long)desc->Height,
              (unsigned long)desc->MipLevels, (unsigned long)desc->ArraySize, (unsigned long)desc->Format, (unsigned long)desc->BindFlags,
              (unsigned long)desc->Usage, (unsigned long)desc->MiscFlags, (unsigned long)GetCurrentThreadId(), save ? "saved" : "-");
    DWORD n = 0, w;
    while (line[n]) n++;
    tx_acquire();
    if (tx_list != (HANDLE)-1) WriteFile(tx_list, line, n, &w, 0);
    tx_release();
    return hr;
}

/* create a throw-away device, redirect CreateTexture2D in the shared vtable */
static int tx_install(char *why)
{
    HMODULE d3d = LoadLibraryW((LPCWSTR)L"d3d11.dll");
    if (!d3d) { wsprintfA(why, "d3d11.dll not loadable"); return 0; }
    tx_d3d11createdevice_fn mk = (tx_d3d11createdevice_fn)GetProcAddress(d3d, "D3D11CreateDevice");
    if (!mk) { wsprintfA(why, "no D3D11CreateDevice"); return 0; }
    void *dev = 0, *ctx = 0;
    int lvl = 0;
    long hr = mk(0, 1 /* hardware */, 0, 0, 0, 0, 7, &dev, &lvl, &ctx);
    if (hr < 0 || !dev) { wsprintfA(why, "D3D11CreateDevice failed %lx", (unsigned long)hr); return 0; }
    void **vt = *(void ***)dev;
    DWORD old, tmp;
    int ok = 0;
    if (VirtualProtect(&vt[5], 8, 0x04 /* rw */, &old)) {
        if (vt[5] != (void *)tx_create) { tx_orig = (tx_create_fn)vt[5]; vt[5] = (void *)tx_create; }
        VirtualProtect(&vt[5], 8, old, &tmp);
        tx_vtable = vt;
        ok = 1;
    }
    typedef unsigned long (*rel_fn)(void *);
    if (ctx) ((rel_fn)(*(void ***)ctx)[2])(ctx);
    ((rel_fn)vt[2])(dev);
    wsprintfA(why, "d3d11 %p, device vtable %p, CreateTexture2D %p -> hook%s", (void *)d3d, (void *)vt, (void *)tx_orig, ok ? "" : " FAILED (VirtualProtect)");
    return ok;
}

static void tx_open_files(void)
{
    DWORD n = GetModuleFileNameW(self_module, tx_dir, 512);
    while (n && tx_dir[n - 1] != '\\' && tx_dir[n - 1] != '/') n--;
    tx_dir[n] = 0;
    wcat(tx_dir, "so4coop_textures");
    CreateDirectoryW(tx_dir, 0);
    uint16_t path[600];
    wcopy(path, tx_dir);
    wcat(path, "\\textures.txt");
    tx_list = CreateFileW(path, 0x40000000, FILE_SHARE_READ, 0, 2 /* CREATE_ALWAYS */, FILE_ATTRIBUTE_NORMAL, 0);
    static const char head[] = "ms_since_hook\thash\tsize\tmips\tarray\tformat(DXGI)\tbind\tusage\tmisc\tthread\tdds\r\n";
    DWORD w;
    if (tx_list != (HANDLE)-1) WriteFile(tx_list, head, sizeof head - 1, &w, 0);
}

/* hotkey thread (research build): install once, then a summary every 30 s */
static void tex_poll(void)
{
    char b[300], why[200];
    if (!tx_tried) {
        tx_tried = 1;
        tx_open_files();
        tx_t0 = tx_report_t = GetTickCount64();
        tx_installed = tx_install(why);
        wsprintfA(b, "  textures: %s (%s); list + .dds in so4coop_textures\\\r\n", tx_installed ? "hook ON" : "hook NOT installed", why);
        logs(b);
        return;
    }
    uint64_t now = GetTickCount64();
    if (tx_installed && now - tx_report_t >= 30000) {
        tx_report_t = now;
        wsprintfA(b, "  [textures t=%lus] created with data %ld (repeats %ld), without data %ld; saved %ld .dds (%lu MB)\r\n",
                  (unsigned long)((now - tx_t0) / 1000), tx_n_data, tx_n_dupes, tx_n_nodata, tx_n_saved, (unsigned long)(tx_bytes_saved >> 20));
        logs(b);
    }
}

/* ---- self-test: hook, then create a 64x64 RGBA texture with data through a second device ---- */
__declspec(dllexport) void WINAPI so4coop_textest(void)
{
    char b[300];
    tex_poll();
    if (!tx_installed) { logs("texture self-test: hook not installed -> FAILED\r\n"); return; }
    tx_d3d11createdevice_fn mk = (tx_d3d11createdevice_fn)GetProcAddress(GetModuleHandleW((LPCWSTR)L"d3d11.dll"), "D3D11CreateDevice");
    void *dev = 0, *ctx = 0, *tex = 0;
    int lvl = 0;
    if (mk(0, 1, 0, 0, 0, 0, 7, &dev, &lvl, &ctx) < 0 || !dev) { logs("texture self-test: second device failed\r\n"); return; }
    static uint32_t px[64 * 64];
    for (int i = 0; i < 64 * 64; i++) px[i] = 0xff000000u | (uint32_t)(i * 2654435761u >> 8);
    TX_DESC d = { 64, 64, 1, 1, 28, 1, 0, 0, 8 /* shader resource */, 0, 0 };
    TX_INIT in = { px, 64 * 4, 0 };
    long before = tx_n_saved;
    long hr = ((tx_create_fn)(*(void ***)dev)[5])(dev, &d, &in, &tex);
    typedef unsigned long (*rel_fn)(void *);
    if (tex) ((rel_fn)(*(void ***)tex)[2])(tex);
    tex = 0;                                              /* v0.41: also a BC3 TYPELESS one (the game's format) */
    static BYTE bc3[16 * 16 * 16];
    for (int i = 0; i < (int)sizeof bc3; i++) bc3[i] = (BYTE)(i * 37 + 11);
    TX_DESC d3 = { 64, 64, 1, 1, 76, 1, 0, 0, 8, 0, 0 };
    TX_INIT in3 = { bc3, 16 * 16, 0 };
    long hr3 = ((tx_create_fn)(*(void ***)dev)[5])(dev, &d3, &in3, &tex);
    if (tex) ((rel_fn)(*(void ***)tex)[2])(tex);
    if (hr3 < 0) hr = hr3;
    if (ctx) ((rel_fn)(*(void ***)ctx)[2])(ctx);
    ((rel_fn)(*(void ***)dev)[2])(dev);
    wsprintfA(b, "texture self-test: CreateTexture2D hr %lx, saved %ld -> %ld: %s\r\n", (unsigned long)hr, before, tx_n_saved,
              hr >= 0 && tx_n_saved == before + 2 ? "WORKS" : "FAILED");
    logs(b);
}
