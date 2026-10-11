# Mario Kart Arcade GP DX: racen met een controller + webcam (plan)

Stand 2026-10-11. Doel van de eigenaar: MKDX kunnen racen met een gewone controller (Xbox of Steam
Controller), en de PS Eye-camera gebruiken voor de spelersfoto van de game. Geen lightgun-game.

Nog niet begonnen met bouwen (een andere sessie werkt tegelijk in `patches/` en de patch-lagen; om
git-conflicten te vermijden wacht de code tot die klaar is). Dit is het plan en de open punten.

## Wat we weten

- Twee versies op de NAS: `MKDX 1.10.22` en `MKDX 1.18.16`. Namco ES3, 32-bit, D3D11, 1360x768.
- TeknoParrot-profielen: `MKDX`, `MKDX118`, `MKDXUSA`, `MKDXUSA106` — EmulationProfile `NamcoMkdx(Usa)`,
  `EmulatorType = TeknoParrot`, **geen Patreon** (dus geen abonnement nodig).
- 1.18.16 heeft `bngrw.dll` (Namco ES3X-laag, zoals Time Crisis 5); die zit nog niet in onze eigen
  OpenParrot-build, dus MKDX houdt voorlopig de lader/kern van de TeknoParrot-runtime.
- Knoppen in het profiel: Test, Service, Coin, Start, **Wheel Axis** (+ Left/Right), **Gas**, **Brake**,
  **Item**, Menu Up/Down, Enter Switch, **Mario Button**, Banapass Card Entry.
- Instellingen: `Input API = DirectInput | XInput`, `CustomResolution`, `Experimental Webcam Hack`,
  en ALL.Net-netwerkvelden (server/gateway/spelers-id) — offline doet de kern dat zelf.
- Camera: NamCam, `Data/Camera/*.canm`; het profiel heeft `Experimental Webcam Hack`.

## Deel 1 — Controller om te racen

Twee mogelijke routes.

**Route A (voorkeur, eerst proberen): de game leest de controller zelf.**
Een Xbox-pad in de Wine-prefix verschijnt via XInput/DirectInput; de TeknoParrot-kern kan assen en
knoppen aan zijn profielinputs binden (Wheel = stick X, Gas/Brake = triggers, Item/Mario/Start/Coin =
knoppen). Dit is hoe het op een pc-kast ook gaat.
- Blokkade in onze directe start: `import-tp-profiles.py` neemt de TeknoParrot-invoertoewijzingen
  (JoystickButtons) met opzet **niet** over — onze invoer staat in `[Input]`/`[Bindings]`. Voor MKDX
  moeten we die bindingen juist wél laten staan (of een eigen `[Input] Route = tp-native` die de
  profielbindingen in `teknoparrot.ini` schrijft en de controller in de prefix laat).
- Steam Controller: werkt alleen als XInput/DirectInput via Steam Input of een driver; een Xbox-pad is
  de makkelijke eerste stap (ruwe XInput). Niet via Steam starten = geen Steam Input-laag.
- Open: hoe de kern precies de controller leest (vraag 82 aan Johan); dode zone / middenkalibratie.

**Route B: RavenGun leest de controller en schrijft de shared memory (zoals de lightgun-routes).**
Consistent met onze aanpak, maar vereist de **NamcoMkdx shared-memory-indeling** (offset/breedte van
wheel/gas/brake, knopbits) — dat is vraag 81 aan Johan, nog niet beantwoord. Pas bouwen als die er is.

Aanbeveling: begin met route A (geen layout nodig), val terug op B als A niet werkt.

## Deel 2 — Webcam (spelersfoto)

- De PS Eye staat als `/dev/video*` op het systeem. MKDX is geen lightgun-game, dus de tracker/camera
  is vrij voor gebruik als webcam (controleer wel dat RavenGun de camera niet claimt tijdens MKDX).
- De game legt een spelersfoto vast via NamCam. Hoe (DirectShow, device-index, resolutie) en wat
  `Experimental Webcam Hack` doet: vraag 83 aan Johan.
- Wine-kant: Wine's video-capture (dshow → v4l2) moet de camera zien. Te testen welke Wine/Proton-versie
  dat ondersteunt. De foto is vermoedelijk optioneel — eerst nagaan of de game zonder camera gewoon
  racet; dan is de webcam een extra, geen blokkade.

## Volgorde zodra we bouwen (vereist de tv + een controller erin)

1. MKDX toevoegen aan RavenGun (`import-tp-profiles.py`, eigen `patches/MKDX*/`), laten starten tot het
   attract/menu. 1.18.16 met de TeknoParrot-lader (bngrw).
2. Route A: profielinvoer behouden/activeren, een Xbox-pad in de prefix, binden aan Wheel/Gas/Brake en
   de knoppen. Op de tv rijden en de assen nameten.
3. Lukt A niet: Johans layout (vraag 81) afwachten en route B bouwen.
4. Webcam: nagaan of de game zonder camera racet; daarna de PS Eye via Wine's capture aanbieden en de
   foto testen.

## Open punten / afhankelijkheden

- Vragen 81, 82, 83 aan Johan (`findings/QA/vragen.txt`).
- De tv en een fysieke controller (en voor de foto de PS Eye) zijn nodig om te verifiëren.
- Een tweede bouwer draait nu niet parallel, om conflicten met de lopende patch-laag-sessie te vermijden.
