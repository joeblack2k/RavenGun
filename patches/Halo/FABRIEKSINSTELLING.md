# De fabrieksinstelling: waar het zit, en waarom er niets te zetten valt (LEZEN)

*RavenGun, 2026-10-13. Leeswerk, geen run, geen bench. Niets is gezet of geschreven. Geen commit.*

## 1. DE FABRIEKSINSTELLING IS EEN **DIAG**-MENU-ITEM (GEMETEN)

In de spelbinary staan de namen bij elkaar:

```
DiagMenuFactorySetup            MenuType_DiagFactorySetup
FactorySetupProc                FactorySetupTextCallback
FactorySetupInit() failed!      DiagMenuGunInfo / DiagGunInfoItems / DiagGunInfoItems_4P
GunInfo_NumAttachedGuns_VALUE   GunInfo_AttachedSerials_VALUE
GunCameraTest_PressServiceToToggleSizeTextCB
QRSetupProc
```

Dus: **de fabrieksinstelling is een item in het DIAG-menu**, naast de geweerinformatie
(`DiagMenuGunInfo`, `GunInfo_NumAttachedGuns`, `GunInfo_AttachedSerials`) en de geweercamera-test.
En in onze eigen indeling is **diag = TEST = bit 0 -> k=4**.

**En dat is precies wat wij nooit gedaan hebben.** De TEST-schakelaar is alleen op het
**kalibratiescherm** geprobeerd (run 11: zes pulsen op alle vier de borden, geen effect) -- nooit in de
**attract-lus**. In run 14, 15 en 16 gebruikten wij juist de **service**-schakelaar (k=3), die niets
deed. De ingang die de binary zelf noemt, hebben we dus in de juiste toestand nog niet geprobeerd.

## 2. ER IS GEEN VLAG OM TE ZETTEN -- DE KAST IS NOOIT INGERICHT (GEMETEN)

Ik heb alle eigen toestandsbestanden van de kast nagelopen:

* **`pmuser/adj/save/`** (de `AdjValStore`-familie, elk bestand met `name`, `version`, `time_set`, `val`):
  `coin0Value` 25, `coin1Value` 25, `coin2Value` 25, `coin3Value` 100, `coinBaseIncUnit` 25,
  `serviceCashbox` 0, `serviceValue` 25, `attractVolume` 90 (version 2), `gameVolume` 90,
  `usePayoutCap` 0. **Geen enkele vlag over de inrichting, het geweer, de camera of de scanners.**
* **`pmuser/aud/save/*.xml`**: `system`, `io_connections`, `io_switch_telem`, `last_connection`,
  `online_ipaddrsports`, `cashbox0/1/2`, `cashbox_reject0`, `eCashbox`, `collection`. Ik heb op
  `factory|setup|provision|webcam|gun|qr|scanner|contract|modem|online|order|present|detect` gezocht:
  **nul treffers** in al die bestanden (de enige treffers waren in `gamelog.txt` -- mijn eigen woorden).
* **`/pm/g7/`** (de bovenlaag van de overlay, waar de kast zou schrijven): **helemaal leeg.** En
  `guninfo.txt` -- het bestand dat de binary zelf noemt (`guninfo_commit: could not open file %s`,
  `/pm/g7/guninfo.txt`) -- **bestaat nergens**.

**Conclusie:** er valt niets om te zetten, want de inrichting is **nooit uitgevoerd**. De kast heeft
geen "compleet"-vlag die op nul staat; de vlag komt pas bestaan als het menu-item gelopen is.

## 3. WAT IK ZOU WILLEN DOEN (en wat ik NIET doe)

**Wat ik voorstel, als korte run (~150 s, beeld op de tv):**
1. naar de **attract-lus** met START (zoals in run 16: herhalen tot de **nieuwe** `attract`-regel er
   staat, met de `NIEUW()`-controle);
2. dan **TEST** (bit 0 -> k=4) met een **lange puls en herhaald** -- dat is de diag-ingang die de binary
   zelf noemt;
3. foto per stap, en de tafelmeting van k=3/k=4/k=0 ernaast;
4. als er een menu verschijnt: navigeren met **SERVICE** (k=3, "volgende item") en **TEST** (k=4,
   "kiezen") -- dat is het klassieke Raw Thrills-patroon en het is te toetsen -- met een foto per stap,
   op zoek naar `Factory Setup`, `Gun Info` en de geweercamera-test.

**Wat ik niet doe zonder jouw akkoord:** het fabrieksinstellingsitem **uitvoeren**. Het menu openen en
fotograferen is lezen; de installatie doorlopen is een wijziging, en die meld ik eerst -- precies zoals
afgesproken. Als het menu-item de keuze biedt om scanners/geweren als **afwezig** te markeren, dan is
dat vermoedelijk de schone oplossing: geen namaak, maar de kast vertellen dat hij ze niet heeft.

**Wanneer het lezen iets oplevert dat ik zou willen zetten**, meld ik het met de bron erbij
(`bestand`, regel, en de huidige waarde), en het zetten gebeurt in een **lokale overlay-kopie** en pas
na akkoord -- de NAS blijft lezen.

## 4. En wat de munt deed

Ter controle: in run 16 bleken de "volumetoetsen" (bit 4/5 -> k=1/k=2) **muntkanalen** te zijn
(`cashbox 0 coin 4`). Dat betekent dat onze tabelindeling op dat punt fout is, en dat de diag- en
service-ingangen dus ook opnieuw getoetst moeten worden -- niet aangenomen. Dat is een tweede reden om
de TEST-route in de attract-lus te proberen in plaats van de service-route.
