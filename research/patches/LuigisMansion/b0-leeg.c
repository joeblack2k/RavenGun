/* B0 (bisectie Luigi's Mansion): de leegst mogelijke DLL MET een DllMain.
 *
 * Doel: meten of het spel een in-process aanhechting verdraagt. Er wordt niets gehaakt,
 * geen thread gemaakt, geen bestand geopend.
 *
 * Waarom DllMain TRUE moet teruggeven: OpenParrotLoader64.exe roept in de standaardroute
 * alleen LoadLibraryW aan (LoadHookDLL -> de bootstrap op addy+30) en kijkt daarna naar
 * het resultaat in RAX. Geeft DllMain FALSE, dan mislukt LoadLibraryW, meldt de lader
 * "Failed to Load DLL! (Error 4)" en beeindigt hij het spelproces met TerminateProcess.
 * Dan meet je de lader, niet de game.
 *
 * Gebouwd met -nostdlib -Wl,--entry=DllMain, dus geen CRT en geen UCRT-imports: de
 * entry point IS DllMain. Dat houdt de indruk in het proces zo klein mogelijk.
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)hinst; (void)reason; (void)reserved;
    return TRUE;
}

/* Dwingt een .reloc af. Zonder die sectie moet de DLL op zijn ImageBase passen; kan Wine
 * dat adres niet geven, dan mislukt LoadLibraryW -- en dan meet je de lader in plaats van
 * de game. Met een .reloc is de DLL verplaatsbaar en is die valkuil weg. */
void *const rg_b0_ankers[2] = { (void *)&DllMain, (void *)&rg_b0_ankers };

