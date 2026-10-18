# HOTD Scarlet Dawn zonder TeknoParrot: geblokkeerd, beslissing eigenaar nodig (2026-10-17)

Volledige uitleg: `docs/research/namco/README.md` in de loader-repo.

* `amdaemon.exe` sterft zonder de kern in 1 s op `amPlatformGetPlatformInformation()`; met de gesloten kern bereikt hij `Advertise` en de
  mount-laag (`AB-UITKOMST.md`). Wat de kern daar teruggeeft is de hardware-identiteit van het platform (`pxGetHwInfo.ini`: `ACA0`).
  Dat nabootsen valt onder de projectgrens "geen patches op keychip-, regio-, integriteits- of dongle-controles"
  (`docs/decisions.md` 2026-10-11): de eigenaar beslist.
* Wel eigen of bijna eigen: schijfletters (patch 0004 van onze kern), twee-exe-start (rg-direct). Nog te bouwen: het USB-IO-bord (HID) in de
  daemon uit `\\.\pipe\TeknoParrotPipe` (indeling `AllsUsbIo`).
