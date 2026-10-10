# RavenGun-plugin voor Unity-arcadegames (route 2)

Een eigen BepInEx-plugin voor de Unity-games van Adrenaline Amusements, zodat RavenGun niet van DemulShooter afhangt en de
eigenaar alle aspecten zelf beheert. Er is één bron en er zijn twee uitvoerbestanden, omdat elke game zijn eigen
`Assembly-CSharp.dll` heeft (zelfde assemblynaam, andere inhoud):

- `RavenGun.TombRaider.dll`: Tomb Raider Arcade
- `RavenGun.RabbidsHollywood.dll`: Rabbids Hollywood Arcade

Besluiten, bronnen en de clean-room-verklaring: `docs/decisions.md` ("Route 2: eigen plugin"). Architectuur:
`docs/architecture.md` ("Unity-route"). Bouwen en installeren: `docs/procedures.md`.

## Wat de plugin doet

- Het I/O-bord van de game uitzetten en vervangen. `KaboomAdrioFxPlusFourFeeders._Initialize` (opent het bord via een native
  bibliotheek) wordt overgeslagen en `ArcadeManager.CheckIOBoard` (toont "IO BOARD ERROR") ook. De game start dus zonder bord.
- Invoer per speler (1 tot 4) uit de daemon: aim in schermpixels, trekker en (Tomb Raider) herladen. De plugin vervangt
  `InputManager.MouseControl` (Tomb Raider) resp. `NewInputManager.MouseControl` (Rabbids) en stuurt de spelers aan zoals de
  game dat met een muis zou doen, maar voor vier spelers.
- Munt, start, test, service en menu: via toetsen (de virtuele toetsen van de daemon: 5 tot 8 munt, 1 tot 4 start, 0 test,
  9 service, pijlen en Enter menu). De toetsen zijn instelbaar. Ze worden omgezet naar dezelfde gebeurtenissen als het
  I/O-bord zou melden (`KaboomOnKeyboardEvent`, `KaboomOnCoinEvent`).
- Uitvoer naar de daemon: recoil (per schot, uit `KaboomManager.StartMotorPwm`), ammo, leven, credits, "speelt" (Tomb Raider)
  of startlamp (Rabbids), en treffers (Tomb Raider).
- Vizier van de game tonen of verbergen (instelbaar; alleen visueel), resolutie en beeldvullend (instelbaar), muisaanwijzer
  verbergen zolang de daemon de aim levert.
- Foutafhandeling: de plugin is TCP-server op 127.0.0.1 (nooit op een ander adres). Zonder daemon, bij een verbroken
  verbinding, bij te late pakketten of bij een poort die bezet is loopt de game gewoon door: de triggers worden losgelaten en
  de game valt terug op de muis; de server blijft proberen. Elke fout in een tik wordt gelogd (één keer per 30 s) en nooit
  doorgegeven aan de game.

## Kanaal (draadprotocol versie 1)

Het protocol is bewust ongewijzigd ten opzichte van wat de daemon (`src/ravengun/games/aagames.*`) al spreekt, zodat de daemon
niet hoeft te veranderen. Zie de kop van `src/common/Protocol.cs` en `docs/architecture.md`. Invoer van de daemon: pakket
van 42 bytes (Tomb Raider) of 38 bytes (Rabbids), zonder kop. Uitvoer van de plugin: int32 lengte, u8 koptype 2, 60 bytes.

## Instellingen

BepInEx maakt bij de eerste start `BepInEx/config/nl.studiodamon.ravengun.<game>.cfg` aan (met uitleg bij elke instelling).
`scripts/aagames-setup.py` schrijft dat bestand alleen als het ontbreekt (poort, resolutie, beeldvullend uit
`packaging/aagames-games.json`); daarna beheert de eigenaar het zelf. Met `--reset-config` wordt het opnieuw gemaakt.

Voornaamste instellingen: `Kanaal/Poort` (standaard 33610), `Scherm/Breedte`, `Hoogte`, `Beeldvullend`, `ResolutieAfdwingen`,
`Vizier/Modus` (FromDaemon, Show of Hide), `Bord/IOBordUitschakelen`, `Toetsen/*` en `Log/Uitgebreid`.

## Bouwen

`scripts/build-unity-plugin.sh [GAME|all]` bouwt in een rootless podman-container (`packaging/containers/unity-plugin/Containerfile`,
Arch Linux met Mono) en refereert read-only aan:

- BepInEx 5.4.21 en HarmonyX uit `~/.local/share/ravengun/bepinex/5.4.21.0/x64/BepInEx/core` (herkomst en SHA-256 in
  `PROVENANCE.txt` daar; download alleen van github.com/BepInEx);
- de eigen game-assemblies op de NAS (`<nas_dir>/Game_Data/Managed`). Die worden niet gekopieerd en niet gewijzigd.

Resultaat: `build/unity-plugin/<GAME>/RavenGun.<GAME>.dll` (door git genegeerd).

## Licentie

De repository heeft nog geen licentie. Wat dat voor deze plugin betekent:

- Onze eigen broncode in `plugins/unity/` is auteursrechtelijk van de eigenaar; kies een licentie voordat je publiceert.
- BepInEx is LGPL-2.1. De plugin linkt er dynamisch tegen (de plugin-DLL verwijst naar `BepInEx.dll`, die niet wordt
  meegeleverd in deze repository maar apart wordt gedownload). Dat is toegestaan zonder dat de plugin zelf LGPL wordt;
  wie BepInEx zelf herverspreidt moet de LGPL-voorwaarden volgen (licentietekst, bron van BepInEx op verzoek, geen
  beperking op het vervangen van de bibliotheek). Wij verspreiden BepInEx niet.
- HarmonyX (`0Harmony.dll`) is MIT: vrij te gebruiken en te herverspreiden met de licentietekst.
- De game-assemblies zijn auteursrechtelijk beschermd en komen nooit in deze repository; ze zijn alleen een referentie bij het
  compileren (interoperabiliteit).
- Er is geen code van DemulShooter gebruikt (clean-room, zie `docs/decisions.md`). De plugin mag dus onder elke gekozen licentie.
