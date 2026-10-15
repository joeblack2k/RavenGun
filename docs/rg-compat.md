# rg-compat: kleine Wine-fixes in het spelproces, en meten zonder debugger

`tools/rg-compat/rg_compat.c` wordt `rg-compat.dll` (i686 of x86_64, `scripts/build-compat.sh --out patches/<Game>/dropin`).
Een game krijgt hem als drop-in met `Preload = true` (`docs/dropins.md`): de OpenParrot-lader laadt hem via `TP_PRELOAD_DLL`
na de kern in het spelproces. Let op: `TP_PRELOAD_DLL` neemt één DLL; een game met al een preload-drop-in kan er niet twee hebben.

Elke fix grijpt alleen in bij precies de situatie die hij oplost; anders gaat de aanroep ongewijzigd door. Geen game-naam, geen
game-adres.

| Fix | Wat Wine anders doet | Gemeten bij |
|---|---|---|
| `GDI32!GetCharABCWidthsA` (importtabel van de exe) | schrijft met een Japans lettertype twee ABC's waar de game ruimte voor één heeft | Action Deka (overgeslagen) |
| `IDirectInput::Initialize(NULL, ...)` via `ole32!CoCreateInstance` (importtabel) | weigert een lege HINSTANCE (`DIERR_INVALIDPARAM`) | After Dark (Virtools): "Cannot Initalize Input Manager" |

## Meetdraad: `RG_COMPAT_SAMPLE`

Met `RG_COMPAT_SAMPLE=<ms>` in `[Env]` start rg-compat een draad die elke `<ms>` milliseconden de andere draden van het proces kort
stilzet (`SuspendThread` + `GetThreadContext`) en hun instructiewijzer telt. Elke 5 s schrijft hij per draad de meest geziene
plekken als `module+RVA` naar `RG_COMPAT_SAMPLE_LOG` (Windows-pad, bv. `Z:\tmp\...\sample.txt`; standaard
`%TEMP%\rg-compat-<pid>.txt` in de prefix). Zo vind je een wachtende of bezige draad zonder ptrace of winedbg (winedbg legt de
games om; `ptrace_scope` is 1).

- De eerste kolom (pc) is betrouwbaar. Een pc in `ntdll`/`win32u` is meestal de terugkeer van een syscall-stub; zoek de naam op
  met de exporttabel van die DLL (bv. `ntdll+0xd110` = `NtWaitForSingleObject`, `win32u+0x11f28` = `NtUserRedrawWindow` in Proton 11).
- De aanroeperkolommen zijn een **stapelscan** (de eerste twee waarden op de stapel die in het hoofdbeeld vallen), geen echte
  stapelwandeling: vaak juist, soms ruis (constanten als 0x989680 = 10.000.000, of adressen in `.data`). Controleer met objdump
  of er vlak voor dat adres een `call` staat.
- Gebruik `+seh` niet samen met de meetdraad: elke `GetThreadContext` schrijft dan een tracelijn.

Gemeten op 2026-10-15: Far Cry Paradise Lost hing onder gamescope in `UpdateWindow` en zonder gamescope in
`ChangeDisplaySettings`; Wartran Troopers wachtte in de hoofddraad op zijn "MAIN LOOP"-draad, die gewoon frames liep.

## perf op een Wine-proces

`perf_event_paranoid` is 2 op de bench: `perf record -e cpu-clock:u -p <pid>` werkt op eigen processen zonder ptrace, ook voor
een kindproces dat rg-compat niet bereikt (Transformers: Shadows Rising: `Shell.exe` start `Transformers2.exe`). PE-modules
hebben geen symbolen voor perf; met een MSVC-`.map` naast de exe maakt een klein script van de ruwe ip's functienamen
(`Publics by Value`, kolom `Rva+Base`).
