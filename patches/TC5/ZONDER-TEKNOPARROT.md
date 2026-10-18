# Time Crisis 5 zonder TeknoParrot: geblokkeerd (2026-10-17)

Volledige uitleg: `docs/research/namco/README.md` in de loader-repo.

* `TimeCrisisGame-Win64-Shipping.exe` praat met het USIO-bord via `WinUsb_*` + `SetupDi*W` + `CM_*` (geen `wajvio`, `bngrw` of `iDmacDrv64`
  in de importtabel van de exe of van `RSLauncher.exe`); de gesloten kern emuleert dat in het spelproces uit `TeknoParrot_JvsState`
  (TeknoParrotUi `TC5Pipe`). Er is geen eigen vervanging; een kandidaat-`game.ini` zou alleen de invoer kwijtraken.
* Transport al gelezen uit `bnusio.dll` (zelfde bibliotheek): bulk-IN/-OUT + interrupt-IN, leesopdracht `{0x10|op,0,addr,len}`,
  schrijfopdracht `{0x90|op,~(addr>>8)&0xF0,addr,len}` + data + 1 byte ack; per ronde 0x100 bytes @0x0000, 0x80 @0x1080, schrijf 0x20 @0x1420.
* Ontbreekt: registerindeling, wat de `CM_*`-boomwandeling in TC5 controleert, en of TC5's `CUsioRaw` gelijk is.
