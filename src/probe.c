/*
 * so4coop probe: a drop-in xinput1_3.dll for STAR OCEAN - THE LAST HOPE 4K & Full HD.
 *
 * It forwards every XInput call to the real system xinput1_3.dll and writes
 * so4coop_probe.log next to itself with:
 *   - the game module (base, size, in-memory sections),
 *   - which controller slots (0-3) the game polls and how often,
 *   - the code addresses (RVA in the game module) that call XInputGetState /
 *     XInputSetState: the entry points for finding the battle control logic.
 * It does not modify the game or its DRM; it only observes the calls it receives.
 *
 * Freestanding (no CRT): builds with clang + lld-link, see build.sh.
 */
#include <stdint.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef void *HANDLE, *HMODULE, *FARPROC, *LPVOID;
typedef const char *LPCSTR;
typedef const uint16_t *LPCWSTR;
#define WINAPI __stdcall
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define ERROR_DEVICE_NOT_CONNECTED 1167
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80

__declspec(dllimport) HMODULE WINAPI LoadLibraryW(LPCWSTR);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleW(LPCWSTR);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameW(HMODULE, uint16_t *, DWORD);
__declspec(dllimport) unsigned WINAPI GetSystemDirectoryW(uint16_t *, unsigned);
__declspec(dllimport) HANDLE WINAPI CreateFileW(LPCWSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) BOOL WINAPI FlushFileBuffers(HANDLE);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) uint64_t WINAPI GetTickCount64(void);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) BOOL WINAPI DisableThreadLibraryCalls(HMODULE);
__declspec(dllimport) int __cdecl wsprintfA(char *, LPCSTR, ...);
__declspec(dllimport) BOOL WINAPI CreateDirectoryW(LPCWSTR, void *);
__declspec(dllimport) HANDLE WINAPI CreateThread(void *, uint64_t, DWORD (WINAPI *)(LPVOID), LPVOID, DWORD, DWORD *);
__declspec(dllimport) void WINAPI Sleep(DWORD);
__declspec(dllimport) BOOL WINAPI MoveFileW(LPCWSTR, LPCWSTR);
typedef struct { WORD y, mo, dow, d, h, mi, s, ms; } SYSTIME;
__declspec(dllimport) void WINAPI GetLocalTime(SYSTIME *);
#define OPEN_ALWAYS 4
#define FILE_APPEND_DATA 0x0004

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; short sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XINPUT_VIBRATION;

typedef DWORD (WINAPI *pGetState)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *pSetState)(DWORD, XINPUT_VIBRATION *);
typedef DWORD (WINAPI *pGetCaps)(DWORD, DWORD, void *);
typedef void  (WINAPI *pEnable)(BOOL);
typedef DWORD (WINAPI *pGetGuids)(DWORD, void *, void *);
typedef DWORD (WINAPI *pGetBattery)(DWORD, BYTE, void *);
typedef DWORD (WINAPI *pGetKeystroke)(DWORD, DWORD, void *);

static HMODULE real;
static pGetState r_GetState, r_GetStateEx;
static pSetState r_SetState;
static pGetCaps r_GetCaps;
static pEnable r_Enable;
static pGetGuids r_GetGuids;
static pGetBattery r_GetBattery;
static pGetKeystroke r_GetKeystroke;

static HANDLE logf = (HANDLE)-1;
static HMODULE self_module;
static int real_tried;
static void load_real(void);
#define ENSURE() do { if (!real_tried) { real_tried = 1; load_real(); } } while (0)
static uintptr_t game_base, game_end;

static void logs(const char *s)
{
    DWORD n = 0, w;
    while (s[n]) n++;
    if (logf != (HANDLE)-1) { WriteFile(logf, s, n, &w, 0); FlushFileBuffers(logf); }
}

/* no CRT: the compiler may still emit these */
void *memset(void *d, int c, unsigned long long n)
{
    volatile unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned long long n)
{
    volatile unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

static void wcat(uint16_t *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = (uint16_t)*s++;
    *d = 0;
}

#include "scan.c"
#include "watch.c"
#include "allrtti.c"
#include "writetest.c"
#include "autocap.c"
#include "stickcap.c"
#include "guardtrace.c"
#include "tagteam.c"
#include "steamctl.c"
#include "steamhook.c"
#include "coopslice.c"
#include "displayfix.c"
#ifndef SO4COOP_RELEASE
#include "texhook.c"
#else
__declspec(dllexport) void WINAPI so4coop_textest(void) { logs("texture self-test: research build only\r\n"); }
#endif

/* ---- statistics (written from the game's input thread, flushed every 2 s) ---- */
#define MAXCALLERS 16
typedef struct { uintptr_t ret; uint32_t hits; } caller_t;
static volatile long st_calls[4], st_ok[4], st_ex[4], st_set[4], total_getstate;
static caller_t callers[MAXCALLERS];
static uint32_t ncallers;
static uint64_t last_flush, first_call;
static DWORD threads[8];
static uint32_t nthreads;
static WORD last_buttons[4];

static void note_caller(uintptr_t ret)
{
    for (uint32_t i = 0; i < ncallers; i++)
        if (callers[i].ret == ret) { callers[i].hits++; return; }
    if (ncallers < MAXCALLERS) {
        callers[ncallers].ret = ret; callers[ncallers].hits = 1; ncallers++;
        char b[160];
        if (ret >= game_base && ret < game_end)
            wsprintfA(b, "new caller: game+0x%08lx\r\n", (unsigned long)(ret - game_base));
        else
            wsprintfA(b, "new caller: %p (outside game module)\r\n", (void *)ret);
        logs(b);
    }
}

static void note_thread(void)
{
    DWORD t = GetCurrentThreadId();
    for (uint32_t i = 0; i < nthreads; i++) if (threads[i] == t) return;
    if (nthreads < 8) {
        threads[nthreads++] = t;
        char b[64]; wsprintfA(b, "polling thread #%u id=%lu\r\n", nthreads, t); logs(b);
    }
}

/* logs the first call of each export (which XInput path the game really uses) */
static volatile long first_seen[16];
static void first_call_of(int id, const char *name, uintptr_t ret)
{
    if (first_seen[id]) return;
    first_seen[id] = 1;
    char b[160];
    if (ret >= game_base && ret < game_end)
        wsprintfA(b, "first call: %s from game+0x%08lx\r\n", name, (unsigned long)(ret - game_base));
    else
        wsprintfA(b, "first call: %s from %p\r\n", name, (void *)ret);
    logs(b);
}
#define FIRST(id, name) first_call_of(id, name, (uintptr_t)__builtin_return_address(0))

static void maybe_flush(void)
{
    uint64_t now = GetTickCount64();
    if (!first_call) first_call = now;
    if (now - last_flush < 2000) return;
    last_flush = now;
    char b[256];
    wsprintfA(b, "[t=%lus] slot calls(connected) 0:%ld(%ld) 1:%ld(%ld) 2:%ld(%ld) 3:%ld(%ld) | Ex:%ld,%ld,%ld,%ld | vib:%ld,%ld,%ld,%ld\r\n",
              (unsigned long)((now - first_call) / 1000),
              st_calls[0], st_ok[0], st_calls[1], st_ok[1], st_calls[2], st_ok[2], st_calls[3], st_ok[3],
              st_ex[0], st_ex[1], st_ex[2], st_ex[3], st_set[0], st_set[1], st_set[2], st_set[3]);
    logs(b);
    for (int i = 0; i < 4; i++) st_calls[i] = st_ok[i] = st_ex[i] = st_set[i] = 0;
}

static void note_buttons(DWORD slot, const XINPUT_STATE *s)
{
    if (slot < 4 && s->Gamepad.wButtons != last_buttons[slot]) {
        char b[96];
        wsprintfA(b, "slot %lu buttons %04x -> %04x\r\n", slot, last_buttons[slot], s->Gamepad.wButtons);
        logs(b);
        last_buttons[slot] = s->Gamepad.wButtons;
    }
}

/* ---- exports ---- */
__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD slot, XINPUT_STATE *s)
{
    FIRST(0, "XInputGetState");
    ENSURE();
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    DWORD r = r_GetState ? r_GetState(slot, s) : ERROR_DEVICE_NOT_CONNECTED;
    total_getstate++;
    if (slot < 4) { st_calls[slot]++; if (r == 0) { st_ok[slot]++; note_buttons(slot, s); } }
    note_caller(ret);
    note_thread();
    maybe_flush();
    return r;
}

/* test hook (not used by the game): run a scan now */
__declspec(dllexport) void WINAPI so4coop_scan(void) { scan_now(); }

DWORD WINAPI XInputGetStateEx(DWORD slot, XINPUT_STATE *s)   /* ordinal 100 */
{
    FIRST(1, "XInputGetStateEx");
    ENSURE();
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    DWORD r = r_GetStateEx ? r_GetStateEx(slot, s) : (r_GetState ? r_GetState(slot, s) : ERROR_DEVICE_NOT_CONNECTED);
    if (slot < 4) { st_ex[slot]++; if (r == 0) note_buttons(slot, s); }
    note_caller(ret);
    maybe_flush();
    return r;
}

__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD slot, XINPUT_VIBRATION *v)
{
    FIRST(2, "XInputSetState");
    ENSURE();
    if (slot < 4) st_set[slot]++;
    note_caller((uintptr_t)__builtin_return_address(0));
    return r_SetState ? r_SetState(slot, v) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD slot, DWORD flags, void *caps)
{
    FIRST(3, "XInputGetCapabilities");
    ENSURE();
    return r_GetCaps ? r_GetCaps(slot, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) void WINAPI XInputEnable(BOOL e)
{
    FIRST(4, "XInputEnable");
    ENSURE();
    char b[48]; wsprintfA(b, "XInputEnable(%d)\r\n", e); logs(b);
    if (r_Enable) r_Enable(e);
}

__declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD slot, void *a, void *b)
{
    FIRST(5, "XInputGetDSoundAudioDeviceGuids");
    ENSURE();
    return r_GetGuids ? r_GetGuids(slot, a, b) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputGetBatteryInformation(DWORD slot, BYTE type, void *info)
{
    FIRST(6, "XInputGetBatteryInformation");
    ENSURE();
    return r_GetBattery ? r_GetBattery(slot, type, info) : ERROR_DEVICE_NOT_CONNECTED;
}

__declspec(dllexport) DWORD WINAPI XInputGetKeystroke(DWORD slot, DWORD res, void *ks)
{
    FIRST(7, "XInputGetKeystroke");
    ENSURE();
    return r_GetKeystroke ? r_GetKeystroke(slot, res, ks) : ERROR_DEVICE_NOT_CONNECTED;
}

/* ---- setup ---- */

static void describe_game_module(void)
{
    HMODULE m = GetModuleHandleW(0);
    BYTE *b = (BYTE *)m;
    int32_t pe = *(int32_t *)(b + 0x3c);
    uint32_t size = *(uint32_t *)(b + pe + 24 + 56);
    uint16_t nsec = *(uint16_t *)(b + pe + 6);
    uint16_t optsz = *(uint16_t *)(b + pe + 20);
    BYTE *sec = b + pe + 24 + optsz;
    game_base = (uintptr_t)b;
    game_end = game_base + size;
    char line[160];
    wsprintfA(line, "game module base=%p size=0x%08lx sections=%u\r\n", (void *)b, (unsigned long)size, nsec);
    logs(line);
    for (uint16_t i = 0; i < nsec; i++, sec += 40) {
        char name[9];
        for (int j = 0; j < 8; j++) name[j] = (char)sec[j];
        name[8] = 0;
        wsprintfA(line, "  %-8s rva=0x%08lx vsize=0x%08lx flags=0x%08lx\r\n", name,
                  *(DWORD *)(sec + 12), *(DWORD *)(sec + 8), *(DWORD *)(sec + 36));
        logs(line);
    }
}

/* v0.10: the log is appended (run 8's log was overwritten by the next launch) and the previous
 * session's so4coop_dumps folder is renamed to so4coop_dumps_before_<this session's start time> */
static char session[32];
static void wcopy(uint16_t *d, const uint16_t *s) { while ((*d++ = *s++)) {} }

static void open_log(HMODULE self)
{
    SYSTIME t;
    GetLocalTime(&t);
    wsprintfA(session, "%04u%02u%02u_%02u%02u%02u", t.y, t.mo, t.d, t.h, t.mi, t.s);
    uint16_t dir[520], from[600], to[600];
    DWORD n = GetModuleFileNameW(self, dir, 512);
    while (n && dir[n - 1] != '\\' && dir[n - 1] != '/') n--;
    dir[n] = 0;
    wcopy(from, dir); wcat(from, "so4coop_dumps");
    wcopy(to, dir); wcat(to, "so4coop_dumps_before_"); wcat(to, session);
    int moved = MoveFileW(from, to) != 0;                  /* fails harmlessly when there is none */
#ifdef SO4COOP_RELEASE
    wcopy(to, dir); wcat(to, "so4coop.log");
#else
    wcopy(to, dir); wcat(to, "so4coop_probe.log");
#endif
    logf = CreateFileW(to, FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    char b[160];
    wsprintfA(b, "\r\n===== session %s =====%s\r\n", session, moved ? " (previous so4coop_dumps renamed to so4coop_dumps_before_<session>)" : "");
    logs(b);
}

static void load_real(void)
{
    static const char *names[] = { "\\xinput1_3.dll", "\\xinput1_4.dll", "\\xinput9_1_0.dll" };
    char line[128];
    for (int k = 0; k < 3 && !real; k++) {
        uint16_t path[520];
        unsigned n = GetSystemDirectoryW(path, 500);
        path[n] = 0;
        wcat(path, names[k]);
        real = LoadLibraryW(path);
        if (real) { wsprintfA(line, "real XInput: system32%s\r\n", names[k]); logs(line); }
    }
    if (!real) { logs("ERROR: no system XInput found\r\n"); return; }
    r_GetState = (pGetState)GetProcAddress(real, "XInputGetState");
    r_GetStateEx = (pGetState)GetProcAddress(real, (LPCSTR)(uintptr_t)100);
    r_SetState = (pSetState)GetProcAddress(real, "XInputSetState");
    r_GetCaps = (pGetCaps)GetProcAddress(real, "XInputGetCapabilities");
    r_Enable = (pEnable)GetProcAddress(real, "XInputEnable");
    r_GetGuids = (pGetGuids)GetProcAddress(real, "XInputGetDSoundAudioDeviceGuids");
    r_GetBattery = (pGetBattery)GetProcAddress(real, "XInputGetBatteryInformation");
    r_GetKeystroke = (pGetKeystroke)GetProcAddress(real, "XInputGetKeystroke");
}

/* own thread: hotkeys work no matter how (or whether) the game reads controllers */
static DWORD WINAPI hotkey_thread(LPVOID unused)
{
    (void)unused;
    uint64_t t0 = GetTickCount64(), last = 0;
    int keys_on = 0;
    Sleep(500);
    watch_setup();
    for (;;) {
        Sleep(30);
        uint64_t now = GetTickCount64();
        /* v0.19: no user32 call (GetAsyncKeyState) during the game's startup. Runs at 01:15, 01:27
         * and 02:42 hung before the title screen; polling keys every 30 ms from 0.5 s after load
         * overlaps the game's window/device setup, a possible race inside Wine. */
        if (!keys_on) {
            if (now - t0 < 45000) continue;
            keys_on = 1;
            logs("hotkeys active (45 s after load)\r\n");
        }
        poll_hotkeys();
        watch_poll();
        autocap_poll();
        guard_poll();
        tagteam_poll();
        steamctl_poll();
        steamhook_poll();
        coop_slice_poll();
        display_fix_poll();
#ifndef SO4COOP_RELEASE
        tex_poll();
#endif
        if (now - last >= 30000) {            /* heartbeat: shows the game is still running */
            last = now;
            char b[96];
            wsprintfA(b, "[alive t=%lus] GetState calls so far: %ld\r\n", (unsigned long)((now - t0) / 1000), total_getstate);
            logs(b);
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        self_module = self;
        {
            BYTE *sb = (BYTE *)self;
            self_base = (uintptr_t)sb;
            self_end = self_base + *(uint32_t *)(sb + *(int32_t *)(sb + 0x3c) + 24 + 56);
        }
        open_log(self);
#ifdef SO4COOP_RELEASE
        logs("so4coop co-op 1.3 loaded (battle co-op for up to 4 players: each controller drives its own character; steady mini-map / target board / sight cursor; R3 = tag-team; F3/F4 = keyboard; settings in so4coop.ini)\r\n");
#else
        logs("so4coop probe v0.41 loaded (BUTTON PROMPT RESEARCH: textures the game creates are listed and saved in so4coop_textures; CO-OP: up to 4 players, time-sliced leader for active players, steady UI leader for map + target board + sight cursor; F5 = UI fix on/off, F6 = slice 40/80/150 ms; F2 toggles, coop_auto=1 starts with battles; TAG-TEAM: R3 = Steam action 10 per controller; hook_steam=1 + leader_only_input=1: only the leader's controller drives; R3 via XInput, F3/F4 = player 1/2; F3/F4 = player 1/2; hotkeys after 45 s; F5/F6 as above; F7 = auto-capture, F8 = WRITE TEST on the leader block at *(CBattleMainTask+0x68) (3 checks), F9 = scan + dump, F10 = mark)\r\n");
#endif
        describe_game_module();   /* the real XInput is loaded on the first call, not under the loader lock */
        CreateThread(0, 0, hotkey_thread, 0, 0, 0);
    }
    return 1;
}
