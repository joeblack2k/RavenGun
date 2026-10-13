# Luigi's Mansion Arcade: kernloze meting van `VACUUM.exe`

*RavenGun, 2026-10-12, gemeten op de Deck. Ruwe gegevens: `~/bw/luigi-kernloos/` (`A.log` volledig,
`A-alleen-VACUUM.log` alleen het spelproces, `B.out`, `corefree_attrib.sh`).*

**Kort:** zonder lader en zonder kern-DLL eindigt `VACUUM.exe` na 0,46 s met exitcode `0x40415820`
(decimaal 1078024224). De "32" uit eerdere notities is alleen de lage byte daarvan. Het proces opent in die tijd
geen enkel eigen bestand en geen COM-poort. Wat de gesloten kern anders doet waardoor het spel daar wel doorloopt,
is hiermee niet gemeten.

## 1. Wat er precies gestart is

```
cd <lokale view>/exe/x64
WINEPREFIX=<verse wegwerp-prefix> WINEDLLOVERRIDES="mscoree=d;mshtml=d" \
WINEDEBUG="+pid,+tid,+timestamp,+seh,+process,+file,+loaddll" \
~/.local/share/ravengun/wine/proton-11.0-2/bin/wine <lokale view>/exe/x64/VACUUM.exe      # run A
wine cmd /c run.bat        # run B: "start /wait "" VACUUM.exe" + "echo FULLEXIT=%ERRORLEVEL%"
```

| | |
|---|---|
| lader | geen (geen OpenParrotLoader, geen BudgieLoader) |
| kern-DLL | geen (geen `TeknoParrot64.dll`, geen `OpenParrot64.dll`) |
| exe | `VACUUM.exe`, 3.436.544 B, sha256 `3dfef015e8657e21046af512561618609b0d230bfe8d5cbd2bf54c101c76e2db` |
| werkmap | een **lokale** map met bestand-symlinks naar de NAS; `Ram/` als lokale kopie, `Log/` lokaal |
| prefix | verse wegwerp-prefix (wined3d, geen DXVK, geen GStreamer-plug-ins, geen instellingen van de gedeelde prefix) |
| beeld | `DISPLAY` en `WAYLAND_DISPLAY` leeg, maar `winewayland.drv` verbond toch met de compositor (standaard-socket in `XDG_RUNTIME_DIR`); er is geen venster gemaakt |
| draaivorm | systemd-user-unit |

De lokale werkmap is bewust: de view `runtime/views/LuigisMansion/exe` is een symlink naar de NAS-map, dus
de gewone werkmap `exe/x64` ligt op de NAS. De vier eerdere pogingen van die avond (22:01–22:17) draaiden nog
wel met die NAS-werkmap; in de NAS-spelmap is in dat tijdvak geen bestand gewijzigd (de drie `Ram/`-bestanden
met mtime 22:22 zijn van een latere run van iemand anders).

## 2. Uitkomst

* **Exitcode.** Run A: Linux-exitstatus 32. Run B: `FULLEXIT=1078024224` = `0x40415820`. In het spoor van run A
  staat `NtTerminateProcess ... exit_code 1078024224` in het proces van `VACUUM.exe` zelf. De code komt dus uit
  het spel, niet uit een lader. De waarde is gelijk aan `ImageBase + 0x415820` zonder de bovenste bit, een adres
  in het nominale `.rdata`-bereik; dat lijkt eerder een achtergebleven pointer dan een bedoelde foutcode
  (afgeleid, niet gemeten).
* **Zelfde code op onze kern.** Eerder die dag (13:04, relay-spoor met onze kern) riep het spel
  `KERNEL32.ExitProcess(140415820)` aan met terugkeeradres `0x140f1374d`, dus vanuit `.capcom1`.
* **Levensduur en draden.** Eén draad, 0,46 s (tijdstempels 45479.810 tot 45480.270).
* **Geladen modules (45).** De importsluiting van de exe plus `winewayland.drv` en `uxtheme.dll`; van het spel
  zelf alleen `D3DCOMPILER_46.dll` uit de spelmap.
* **Bestanden.** Alleen het zoeken naar DLL's (eerst spelmap, dan `system32`), `tzres.dll`, de fontmap,
  `system.ini` en het thema. Geen `Data/`, geen `Ram/`, geen `LMAIO.bin`, geen `COM`-naam, geen netwerk.
  In de lokale werkmap is niets geschreven.
* **De laatste 220 ms**, alles in draad `00e8` van het spelproces:

```
45480.049  NtQueryInformationProcess klasse 0x07 (ProcessDebugPort), tweemaal
45480.049  NtQueryInformationProcess klasse 0x1e (ProcessDebugObjectHandle)
45480.262  EXCEPTION_SINGLE_STEP op VACUUM.exe+0x122EA12 (.capcom1), eigen handler op 0x140D8BEE0 (.capcom0),
           verder op 0x14122EA1D
45480.262  NtQueryInformationProcess klasse 0x15 en 0x24
45480.270  NtTerminateProcess, exit_code 1078024224
```

## 3. De importtabel staat gewoon in het bestand

De datadirectory's `import` (RVA `0x10980d8`), `iat` (`0x120d000`), `export`, `exception` en `loadconfig` liggen
allemaal in `.capcom1`, de enige sectie met bytes op schijf, en zijn statisch te lezen. De importtabel heeft
23 descriptors met samen 415 functies. De eerste 18 zijn van het spel (o.a. `KERNEL32` 153, `USER32` 49,
`WS2_32` 19, `ADVAPI32` 14, `ole32` 8, `bcrypt` 7, `WINMM` 5, `SETUPAPI` 4, `wlanapi` 4, `DINPUT8` 1,
`IPHLPAPI` 1, `D3DCOMPILER_46` 2). Daarna volgen nog vijf descriptors voor `KERNEL32`, `USER32` en `ADVAPI32`
met o.a. `CreateToolhelp32Snapshot`, `Thread32First`, `TerminateProcess`, `EnumServicesStatusExW`,
`OpenSCManagerW`, `GetProcessAffinityMask` en `MessageBoxW`. De exe exporteert 57 `curl_*`-functies onder de
naam `VACUUM_FINAL.exe`. Er is geen TLS-directory.

## 4. Wat dit wel en niet zegt

* De COM-poort is niet de oorzaak van dit einde: het spel is weg voordat het een COM-naam opent. Dat past bij de
  meting met onze kern en patch 0005, die het einde ook niet wegnam.
* Het einde valt in de stub (`.capcom0`/`.capcom1`), na twee debugger-vragen en een eigen single-step-val. Dat is
  beschermingscode. Welke controle faalt is niet vastgesteld, en dat deel is overgedragen (taakverdeling eigenaar).
* Niet gemeten: of `.text` op dat moment al uitgepakt is, waar het OEP ligt, en wat de gesloten kern in dezelfde
  220 ms anders doet. De eerstvolgende meting is hetzelfde spoor (`+pid,+seh,+process,+loaddll`) op de gesloten
  kern: komen de drie vragen en de single-step daar ook, en wat volgt er dan in plaats van `NtTerminateProcess`.
* Een geheugenbeeld van een kernloze run is niet gelukt: het venster is 0,46 s en de dumper miste het.
  In het proces met de gesloten kern (PID 363640) stond op het startpunt `0x140f23125` de sprong
  `mov rax, 0x6ffff6d4e980; jmp rax`; het doel ligt in het anonieme blok direct achter de kop van
  `TeknoParrot64.dll`. Of dat de kern is of het uitgepakte spel is niet vastgesteld.

## 5. Ingetrokken

Een eerdere melding uit dezelfde avond, "kernloos initialiseert het spel OLE, GUI en I/O-draden en sluit daarna
zijn I/O netjes af", klopt niet. Die regels kwamen uit een spoor zonder `+pid` en hoorden bij andere processen in
de prefix (`tabtip.exe`, `services.exe`, `rpcss.exe`, `winedevice.exe`). Ook "kernloos komt het spel verder dan op
onze kern" vervalt daarmee: beide eindigen vroeg, met dezelfde code.
