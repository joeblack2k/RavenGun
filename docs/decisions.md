# Beslissingen en afgewezen hypotheses

## 2026-10-07 — Uitvoeringsroute Halo

Vastgesteld: Halo is een native Linux-ELF (platform A = Raw Thrills Linux). De TeknoParrot-UI is native Linux (B), maar de loader voor dit profiel, ElfLdr2/BudgieLoader, is een Windows-component die de Linux-UI via Wine/Proton draait (C). De keten wordt dus: Linux-UI, Wine/Proton, BudgieLoader.exe, Linux-ELF van de game.

Gekozen standaardroute: TeknoParrot (de route uit de projectopdracht), met de officiële ElfLdr2- en Proton-componenten die de UI zelf beheert, in een lokale kopie.

Niet als standaard gekozen: de map bevat `halo.so`, een LD_PRELOAD-dongle-emulator (Sentinel HASP) van onbekende herkomst, met een aangepaste `run_game.sh` (`LD_PRELOAD=halo.so game2`). Dit omzeilt de licentiebeveiliging van de game buiten TeknoParrot om en valt onder "omzeil geen licentie/activatie" uit de opdracht. Daarnaast is het onbekende code met hooking-functionaliteit. Deze route wordt alleen heroverwogen als de gebruiker daar uitdrukkelijk voor kiest.

Update 2026-10-07: de gebruiker heeft uitdrukkelijk voor de halo.so-route gekozen. Die is nu de werkroute, zonder verdere patches aan de beveiliging of controles van de game.

Gecontroleerd en afgewezen: ElfLdr2 staat niet in de UI-map, en er is geen native Linux-ElfLdr2 gevonden in de strings van de UI-binary.

## 2026-10-07 — Na de eerste zichtbare start

Hypothese "FileTest faalt door een beschadigde kopie": afgewezen. De log van de game noemt alleen de drie aangepaste bestanden.

Hypothese "START/TEST via het toetsenbord": niet bevestigd. XTEST-toetsen geven geen reactie; een fysieke toetsdruk moet dit nog uitsluiten.

Nieuwe hypothese: de kastinvoer loopt via een FTDI-USB-I/O-bord (libftdi/libusb statisch gelinkt). Mogelijke routes:
- Een eigen I/O-bord-emulatie in RavenGun. Dat is meteen het natuurlijke koppelpunt voor de Move in fase 3, maar vereist onderzoek naar het I/O-protocol (interoperabiliteit, geen DRM) en een manier om een USB-apparaat aan te bieden (bijvoorbeeld USB/IP met vhci-hcd: een kernelmodule en rootrechten, dus toestemming nodig).
- De TeknoParrot-route (Patreon-abonnement), waarin het I/O-bord al geëmuleerd wordt.

## 2026-10-07 — Kern van ravengund (fase 2B/2F)

Uitgewerkt in `docs/architecture.md`. De belangrijkste keuzes:

- Eén daemon is eigenaar van alle hardware. Clients praten alleen via een Unix-socket (0600 in een map met 0700, plus een uid-controle via SO_PEERCRED) met getypeerde JSON-commando's, één per regel. Er is geen HTTP in de live keten.
- De pijplijn heeft één eigenaar (de HID-thread). Beheeropdrachten gaan via een wachtrij naar die thread. De IPC-thread leest alleen een gepubliceerde momentopname. Zo zijn er geen locks in de rekenroute en kan een trage client niets blokkeren.
- Elk HID-rapport wordt afzonderlijk verwerkt, met beide IMU-subsamples (`Frame_FirstHalf` en daarna `Frame_SecondHalf`) en de echte ontvangst-dt, verdeeld over de twee subsamples. Er wordt geen samplefrequentie aangenomen.
- Knopflanken komen per rapport, en daarnaast zijn er per knop cumulatieve tellers. Daardoor gaat bij meerdere of trage afnemers geen druk of loslaten verloren.
- Fusie: een Mahony-filter met alleen een P-term (IMU-only), met een aparte biasschatter die alleen bij stilstand bijwerkt. Gekozen boven Madgwick omdat de correctie eenvoudig te controleren is en de bias dan op één expliciete plek wordt geschat, in plaats van ook nog in een integrator-term. De magnetometer staat uit (ongekalibreerd).
- De aim werkt voorlopig in de oriëntatiemodus: yaw en pitch t.o.v. het midden, lineair naar [0,1]. Een geometrische aim-ray volgt pas met camera en kalibratie.
- Eén filterlaag: 1€ op de aim, uit te schakelen. PSMoveAPI filtert niet (zijn eigen oriëntatie staat uit).
- Een eenmalig automatisch midden per daemon-run is zichtbaar als `center_source: auto_initial`. Daarna komt een nieuw midden alleen van een expliciete `recenter`. Er wordt nooit stil gerecenterd.
- Er is een eigen kleine JSON-parser (ongeveer 250 regels, getest) in plaats van een gevendorde library. Dat is genoeg voor het kleine protocol en voegt geen afhankelijkheid toe.

Afgewezen:
- De bias continu schatten via de integrator-term van Mahony. Die maakt yaw-bias niet waarneembaar zonder magnetometer en verbergt wat er gebeurt.
- Bij stilstand de integratie stoppen (zero-velocity clamp). Dat zou ook langzame echte rotatie en de random walk maskeren, en geldt dus als verborgen driftcorrectie.
- De rapporttijd vervangen door sequence × nominale periode. Het 4-bitsvolgnummer slaat regelmatig nummers over, en de betekenis daarvan is nog niet bevestigd.

## 2026-10-07 — Wine-runner voor Halo in TeknoParrot

Het Proton-script (GE-Proton11-7) is afgewezen voor Halo. De JVS-brug van TPUI 2.0.0.130 detecteert dan het spelproces niet (zoekt comm "game", terwijl er alleen BudgieLoader_x64 draait; zie ProtonProcessDetector.cs en ProtonLauncher.cs in de branch net8-migration). Gekozen is een plain-wine-runner, alleen voor dit profiel: Kron4ek wine-proton-11.0-2-amd64. Kron4ek vanilla 11.19 is afgewezen: daarmee draaide de game zonder venster. Het upstream-issue moet nog gemeld worden: ExpectedExecutable voor ElfLdr2 hoort de loader te zijn.
