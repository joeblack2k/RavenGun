#include <windows.h>
#include <stdio.h>
typedef HANDLE (WINAPI *pf)(const GUID*, const char*, HWND, DWORD);
int main(void) {
    HMODULE m = LoadLibraryA("setupapi.DLL");
    printf("LoadLibraryA(setupapi.DLL) = %p (fout %lu)\n", (void*)m, (unsigned long)GetLastError());
    if (m) {
        pf f = (pf)GetProcAddress(m, "SetupDiGetClassDevsA");
        printf("GetProcAddress = %p\n", (void*)f);
        if (f) { HANDLE h = f(NULL, NULL, NULL, 0); printf("aanroep = %p\n", h); }
    }
    return 0;
}
