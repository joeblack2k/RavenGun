#include <windows.h>
#include <stdio.h>
int main(void) {
    HMODULE m = LoadLibraryA("ravengun-patchmem32.dll");
    printf("motor geladen: %p\n", (void*)m);
    Sleep(3000);   /* tijd voor de draad */
    return 0;
}
