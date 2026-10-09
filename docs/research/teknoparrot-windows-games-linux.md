# TeknoParrot Windows-games op Linux/Steam Deck: onderzoek (2026-10-09)

Methode: alleen WebSearch/WebFetch. Zoekresultaten waren zeer mager; veel forums (Reddit, TP-forum, Discord, sindenwiki, X) waren niet leesbaar (403/402) of leverden niets. Een bron wordt alleen genoemd als de pagina echt is geopend; bij zoekresultaten die ik niet zelf opende staat dat erbij. Markering: [BRON] = uit bron gelezen, [AFGELEID] = afgeleid/onzeker.

## 1. Windows-games via TP op Linux, en "Failed to find kernel32.dll in target process"

- [BRON] Er is een open TeknoParrotUI-issue #1443 (15 juli 2026): Battle Gear 4 Tuned op de native Linux-build 2.0.0.79 met GE-Proton11-1 (gedownload via de TP-opties). pipehelper.exe, gestart via de Proton-wine, stopt direct met "wine: Bad server socket 11 : Bad file descriptor" (geërfde WINESERVERSOCKET=11 is ongeldig); de JVS-pipe "TeknoParrot_JVS" wordt dus nooit gemaakt. Reporter: andere Wine-versies geven hetzelfde. Geen reacties, geen workaround. https://github.com/teknogods/TeknoParrotUI/issues/1443
- [BRON] Dit past bij onze waarneming "JVS niet verbonden" bij alle Windows-games: het kan een bekend, onopgelost Linux-probleem van de JVS-brug zijn, los van de games. [AFGELEID] Geen bevestiging dat het dezelfde oorzaak is; controleer in onze logs of pipehelper.exe ook bij ons met die foutmelding eindigt.
- [BRON] Issue-lijst TeknoParrotUI (zoekopdracht "wine"): PR #1152 "Improve DInput control input fields ... compatibility with wine" (gemerged 28 april 2025). https://github.com/teknogods/TeknoParrotUI/issues?q=wine . OpenParrot-issue #322 "Linux version of TeknoParrot" (open, geen maintainerreactie, geen Wine-tips): https://github.com/teknogods/OpenParrot/issues/322
- [BRON] Release-pagina TeknoParrotUI: 2.0.0.131 (net8) noemt alleen "Port committed 1.0 updates through bf5a7b6 to 2.0"; geen Linux/Proton/WineRunnerPath in de notities. https://github.com/teknogods/TeknoParrotUI/releases . Het README noemt de projecten alleen "for Windows". https://github.com/teknogods/TeknoParrotUI
- [BRON] Voor de exacte melding "Failed to find kernel32.dll in target process" is geen enkele webpagina gevonden (zoekresultaat gaf alleen andere Wine-kernel32-fouten). Er is dus geen gedocumenteerde oplossing gevonden. Onze eigen omzeiling (TP_POSTSTART_REMOTETHREAD_MS) staat nergens online die ik vond; [AFGELEID] hij komt uit de loader-strings.
- [BRON, alleen zoekresultaat-samenvatting, X-post zelf niet leesbaar] Een gebruiker (AllieRX) meldt dat TP-UI via Proton draait maar geen games; de samenvatting noemt als fix d3dcompiler_47 en d3dcompiler_43 via protontricks plus GE-Proton. Anekdote, niet geverifieerd. https://x.com/AllieRX/status/1989210480796135883 (fetch gaf 402).
- Aangeraden Proton/Wine-versies, WINEDLLOVERRIDES of 32/64-bit-prefix-instellingen specifiek voor TP: niet gevonden.

## 2. Per game

Algemeen [BRON, via zoekresultaat-samenvatting van sindenwiki.org/wiki/TeknoParrot, pagina zelf gaf 403]: TP vraagt DirectX End-User Runtimes (juni 2010) en Visual C++ AIO-redistributables; antivirus-uitzondering; Windows-schaling 100%. Voor Linux: [AFGELEID] d3dx9_43 en vcrun in de prefix zijn dus de logische eerste stap.

- Tomb Raider Arcade en Rabbids Hollywood (Unity, Adrenaline Amusements): [BRON] DemulShooter-wiki (hotr.6bolt.com): beide hebben een eigen item; advies "Demulshooter met Stand Alone" of TeknoParrot zonder inputs plus DemulShooter. https://hotr.6bolt.com/pmwiki.php/GameFiles/DemulShooter . [BRON] LaunchBox-forum: Tomb Raider kan standalone zonder TP draaien (DemulShooter-wiki aangeraden); in het topic niets over IO BOARD ERROR of eisen. https://forums.launchbox-app.com/topic/83802-tomb-raider-on-teknoparrot-scale-issue/ . [AFGELEID] Het patroon (BepInEx/Doorstop-bestanden uit DemulShooter naar de gamemap) suggereert dat IO-emulatie bij deze Unity-games via BepInEx-plugin loopt, en dat "IO BOARD ERROR" betekent dat die plugin/JVS-link niet verbindt; onze waarneming (Base not found, geen pipe) past daarbij. Oorzaak van Rabbids na ~45 s stoppen: niets gevonden.
- Luigi's Mansion Arcade: [BRON] DemulShooter-wiki noemt alleen "rumble feature with light and vacuum". Geen eisen, geen VACUUM.exe-fix gevonden.
- Time Crisis 5: [BRON] DemulShooter-wiki: TeknoParrot verbindt met COM4 voor het geweer en kan daarom niet voor de lichtgun worden gebruikt; er is een aparte stand-alone-regel (tc5_ds.txt). [AFGELEID] COM-poortafhankelijkheid (COM4) kan onder Wine een eis zijn (symlink naar /dev/ttyS*/pty in de prefix); niet bevestigd.
- Let's Go Safari: [BRON] niet vermeld op de DemulShooter-wiki. Verder niets gevonden.
- Transformers Shadows Rising: [BRON] DemulShooter-wiki: alleen bestand tsr.txt, geen notities. Voor de fx_2_0-melding: [BRON, zoekresultaat] algemene Wine-meldingen dat d3dx9_43 vaak samen met d3dcompiler_43 nodig is en dat "fx_2_0 not yet supported" een shadercompile-melding is (WineHQ-bugs; pagina's zelf niet geopend). [AFGELEID] winetricks d3dx9_43 d3dcompiler_43 d3dcompiler_47 in de TP-prefix is de voor de hand liggende stap; Wine's eigen d3dx9 ondersteunt effect-profiel fx_2_0 mogelijk niet volledig, native DLL vervangt dat.
- Meldingen van werkend krijgen op Linux/Steam Deck voor deze zes games: geen gevonden.

## 3. Redistributables zonder systeemwijzigingen

- [BRON] Protontricks (https://github.com/Matoking/protontricks): Flatpak `com.github.Matoking.protontricks` wordt aangeraden voor Steam Deck; `STEAM_COMPAT_DATA_PATH` (prefix = $STEAM_COMPAT_DATA_PATH/pfx) wordt ondersteund; `$WINETRICKS` wijst naar een eigen winetricks; `$PROTON_VERSION` pint de Proton-versie; de Flatpak-sandbox ziet standaard alleen de Steam-map, extra paden (zoals ~/.local/share/TeknoParrotUI en de NAS) vergen een filesystem-override (flatpak override --user).
- [BRON] Winetricks (https://github.com/Winetricks/winetricks): script los downloaden en chmod +x zonder root; `--self-update` zonder sudo. De pagina documenteert WINEPREFIX/WINE niet.
- [AFGELEID] Werkwijze zonder rootrechten: winetricks als losse gebruikersscript, `WINEPREFIX=~/.local/share/TeknoParrotUI/prefixes/shared/proton/pfx WINE=<pad naar Kron4ek wine> winetricks -q d3dx9_43 d3dcompiler_47 vcrun2019`. Risico: de Proton-prefix is door Proton gemaakt; plain-wine-aanpassingen kunnen die prefix beschadigen, dus eerst een kopie van de prefix (of een aparte prefix voor de wine-runner) gebruiken. Niet getest.

## Vervolgstappen per game (eerst proberen bovenaan)

Alle games: 1) controleer of pipehelper.exe in onze logs ook "Bad server socket" geeft (issue #1443); zo ja is JVS-bridge op Linux een algemeen blokkerend probleem en moet dat eerst opgelost of op #1443 gewacht worden. Effect: verklaart "JVS niet verbonden" bij alle games. Risico: geen (alleen lezen). 2) Prefix-kopie maken en winetricks d3dx9_43, d3dcompiler_43/47, vcrun2019 installeren in de kopie. Risico: prefix stuk, daarom kopie.

Transformers: 1) d3dx9_43 + d3dcompiler_43 + d3dcompiler_47 in een prefix-kopie; verwacht dat fx_2_0 verdwijnt; risico: andere shaderfouten. 2) WINEDEBUG=fixme+d3d voor details.

Tomb Raider: 1) controleer BepInEx/Doorstop-plugin-log (BepInEx/LogOutput.log) of de IO-plugin laadt; 2) probeer na JVS-fix opnieuw; 3) als alternatief standalone met DemulShooter-bestanden (Windows-tool, onder Wine onbekend). Risico: wijzigingen in gamemap op de NAS, werk met de --view-symlinkmap.

Rabbids Hollywood: zelfde als Tomb Raider; log uit BepInEx bekijken rond de 45 s-stop (mogelijk licentie/IO-timeout; [AFGELEID]).

Time Crisis 5: COM4 beschikbaar maken in de prefix (wine registry Ports/symlink dosdevices/com4); verwacht dat RSLauncher niet meer direct stopt; risico: onbevestigd.

Luigi's Mansion en Let's Go Safari: geen aanwijzing gevonden. Eerst na stap 1 en prefix-redistributables opnieuw testen; daarna andere Proton (GE-Proton nieuwer of oudere) proberen.

## Niet gevonden

Elke bron die de kernel32-melding behandelt; officiële TP-Linux-documentatie (teknoparrot.com/en/docs gaf 404, wiki niet gevonden); Reddit/Discord/forumthreads (niet doorzoekbaar via de tools); per-game eisen voor de zes titels; "IO BOARD ERROR"-oorzaak; Rabbids 45 s; meldingen van werkende Linux-sessies; aanbevolen Proton-versie en TP-instellingen.
