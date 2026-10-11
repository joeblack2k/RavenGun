# NERF Arcade

Instellingen: `game.ini`. Dit bestand gaat over de **dongle** van deze game.
Bevindingen en meetgeschiedenis: `findings/NERF.md` (op de NAS naast de games, niet in deze repo).

## Vraagt deze game een dongle? — JA, maar in MANAGED CODE, en TeknoParrot levert daar een plugin voor

**Er is hier GEEN HASP-drop-in gebouwd, en dat is met opzet.** Bewijs:

* `Nerf.exe` (de Windows-launcher, PE32+ x86-64, 648.704 bytes) bevat **0** treffers voor `hasp`.
  De spellogica zit in de Unity-assemblies, niet in deze launcher.
* De dongle wordt in **managed code** gevraagd. In `Nerf_Data/Managed/Assembly-CSharp.dll` staan:
  * de klasse `UDongleControl` met `DongleInit`, `DongleControl`, `DonglePresent`, `DongleThread`,
    `DongleReadWrite`, `DongleVersionOkay`, `CheckDongle`, `FinalizeDongle`, `GetDongleVersion`,
    `SetHaspLoginDelegate`, `SetHaspLogoutDelegate`, `SetHaspReadDelegate`, `SetHaspWriteDelegate`
  * de constanten `HASP_FILEID_RO`, `HASP_FILEID_RW`, `HASP_INACTIVE`, `HASP_NOT_FOUND`
  * de P/Invoke-bestemming **`hasp_linux_23714`** — dat is de Linux-client
    `Plugins/x86_64/libhasp_linux_23714.so` (ELF 64-bits, SONAME `libhasp_linux_x86_64_23714.so`,
    28 `hasp_`-exporten). Die hoort bij de **Linux**-build van de game, niet bij de
    TeknoParrot/Wine-route.
* Onder TeknoParrot/Wine kan die P/Invoke niet oplossen (er is geen `hasp_linux_23714.dll` in de
  spelmap; de enige HASP-bestanden daar zijn de twee `.so`-kopieën onder `Plugins/x86_64/` en
  `Nerf_Data/Plugins/x86_64_ori/`). **TeknoParrot levert daar een BepInEx-plugin voor:**
  `BepInEx/plugins/NERFTPPlugin.dll` (20.992 bytes) bevat `HaspLogin`, `HaspLogout`,
  `HaspReadCabSerialNum`, `HaspReadCabTemplate`, `HaspReadCabType`, `HaspReadCountryCode`,
  `HaspReadDongleVersion`, `HaspReadLifetimeCoinCount` en de bijbehorende `HaspWrite…`-varianten, en
  verwijst naar de Harmony-doelen `UDongleControl, Assembly-CSharp … HaspLogin` en
  `… hasp_read`. De plugin beantwoordt de dongle-aanroepen dus in managed code.

## Gevolg

* **Geen `dropin/` en geen `dongle.json` voor deze game.** Er is geen HASP-API-oppervlak in het
  Windows-spel om te onderscheppen: de aanroepen zijn managed en worden al door TeknoParrot's eigen
  plugin afgehandeld. Een HASP-emulator zou hier niets te doen hebben.
* **Wat deze game nodig heeft is een plugin-route**, niet een dongle-image: de BepInEx-plugin van
  TeknoParrot (die er al is en werkt — de game is speelbaar, zie `game.ini`, `[Status]`).
* De Linux-client `libhasp_linux_23714.so` is er wel, maar er is **geen enkele bron** voor de
  gegevens die erbij horen: geen `haspid`, geen fileid, geen image. De hele NAS-boom is doorzocht op
  de bestandsnaamregel van het HASP-formaat (32 tekens 0/1), op hex-fileid-namen, op bestanden van
  112 bytes, op mappen `*hasp*`/`*dongle*` en op `hasp.bin`; de enige dongle-images daar zijn die van
  Halo Fireteam.

## Wat NIET is vastgesteld

* Welke waarden `NERFTPPlugin.dll` teruggeeft (dat is TeknoParrot's implementatie, niet de echte
  dongle) en of die overeenkomen met wat een echte kast heeft.
* Of de Linux-build van deze game ooit nodig is; zo ja, dan hoort daar een 64-bits HASP-drop-in bij,
  maar de gegevens daarvoor zijn er niet.
