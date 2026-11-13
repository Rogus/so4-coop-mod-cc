/* Test harness: loads xinput1_3.dll from its own folder (like the game does)
 * and polls slots 0-3 for a few seconds. Console x64, freestanding. */
#include <stdint.h>
typedef unsigned long DWORD; typedef unsigned short WORD; typedef unsigned char BYTE;
typedef void *HANDLE, *HMODULE, *FARPROC;
#define WINAPI __stdcall
__declspec(dllimport) HMODULE WINAPI LoadLibraryW(const uint16_t *);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE, const char *);
__declspec(dllimport) HANDLE WINAPI GetStdHandle(DWORD);
__declspec(dllimport) int WINAPI WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) void WINAPI Sleep(DWORD);
__declspec(dllimport) void WINAPI ExitProcess(unsigned);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
typedef struct { DWORD pkt; WORD buttons; BYTE lt, rt; short lx, ly, rx, ry; } ST;
typedef DWORD (WINAPI *GS)(DWORD, ST *);
static void out(const char *s) { DWORD n = 0, w; while (s[n]) n++; WriteFile(GetStdHandle((DWORD)-11), s, n, &w, 0); }
void mainCRTStartup(void)
{
    static const uint16_t name[] = { 'x','i','n','p','u','t','1','_','3','.','d','l','l',0 };
    HMODULE h = LoadLibraryW(name);
    char b[160];
    if (!h) { out("LoadLibrary failed\n"); ExitProcess(1); }
    GS gs = (GS)GetProcAddress(h, "XInputGetState");
    for (int it = 0; it < 150; it++) {           /* ~3 s at 50 Hz, like a game loop */
        for (DWORD s = 0; s < 4; s++) { ST st; gs(s, &st); }
        Sleep(20);
    }
    void (WINAPI *scan)(void) = (void (WINAPI *)(void))GetProcAddress(h, "so4coop_scan");
    if (scan) { out("running so4coop_scan...\n"); scan(); }
    for (DWORD s = 0; s < 4; s++) {
        ST st; DWORD r = gs(s, &st);
        wsprintfA(b, "slot %lu: %s  buttons=%04x lx=%d ly=%d\n", s, r == 0 ? "CONNECTED" : "-", r == 0 ? st.buttons : 0, st.lx, st.ly);
        out(b);
    }
    ExitProcess(0);
}
