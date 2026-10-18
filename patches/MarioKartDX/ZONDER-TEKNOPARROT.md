# Mario Kart DX zonder TeknoParrot (standaard sinds 2026-10-17)

Volledige uitleg: `docs/research/namco/README.md` in de loader-repo.

* `game.ini`: `[Loader] Core = ravengun` (onze OpenParrot-kern, generieke route). De gesloten kern is niet meer nodig; het spel speelt offline.
* `dropin/`: `rg-mfcam.dll` (camera, d3dx10-pHResult en JVS: `CreateFileA("COM3")` -> `\\.\pipe\TeknoParrot_JVS`, sense-lijn uit
  `TeknoParrot_JvsState[0]`; zet zichzelf uit als de TeknoParrot-kern geladen is) en `dropin.ini` met `Env = RG_JVS_COMPORT=COM3`.
* `options.json`: optie **Online play** (`ravengun.core` -> `[Loader] Core`): `teknoparrot` zet de gesloten kern terug voor online spelen op de
  gemeenschapsserver van TeknoParrot (PlayerId onder Network); dat vraagt de TeknoParrot-map in de runtime van RavenGun.
* ONLINE zonder de gesloten kern bestaat niet: het COM-object `IAuth` van `AMCUS/AMAuthd.exe` (CLSID `{4603BB03-058D-43D9-B96F-639BE908C1ED}`, IID
  `{045A5150-D2B3-4590-A38B-C1158678E1AC}`, 29 methodes) en het geauthenticeerde TeknoParrot-protocol (TPAUTH) zijn niet nagebouwd.
