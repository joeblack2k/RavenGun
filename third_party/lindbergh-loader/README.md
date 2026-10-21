# lindbergh-loader: de Lindbergh-laag, op je eigen machine gebouwd

Doel: de Sega Lindbergh-games (HOTD4, HOTD4 Special, HOTD EX, Too Spicy, Rambo, Ghost Squad Evolution, Primeval Hunt) native uit
RavenGun starten, zonder TeknoParrot. Een Lindbergh-game is een gewone i386-ELF; wat ontbreekt is de hardware om hem heen
(baseboard met JVS, EEPROM, de dongle op het baseboard, de controles op een NVIDIA-kaart). lindbergh-loader levert dat als
LD_PRELOAD-bibliotheek (`lindbergh.so`) plus vervangers voor de bibliotheken van het systeem (`libsegaapi.so`, `libkswapapi.so`,
`libposixtime.so`). Hoe RavenGun hem gebruikt: `docs/lindbergh.md`.

- Bron: https://github.com/lindbergh-loader/lindbergh-loader, gepind op commit `0af606d845b70339c335785c0eba68b47b78df3c`
  (5 mei 2026, master). Copyright de Lindbergh Loader Development Team.
- **Licentie: Creative Commons Attribution-NonCommercial-ShareAlike 4.0** (`LICENSE.md` sinds "Release V2", 23 december 2024; daarvoor
  had de bron geen licentie, dus alle rechten voorbehouden). De README van de bron vraagt daarnaast toestemming voor gebruik in
  pay-to-play, abonnementen of andere commerciële doeleinden.

## Licentiebesluit: niet in de repo, wel in de app

CC BY-NC-SA 4.0 is **niet verenigbaar met de GPL-3.0-or-later** van RavenGun: de NC-clausule is een extra beperking die de GPL
verbiedt (GPL-3.0 §7 en §10), en de ShareAlike van CC BY-SA 4.0 is alleen eenrichtingsverenigbaar met de GPL-3.0 voor BY-SA, niet voor
BY-NC-SA (Creative Commons noemt geen verenigbare licentie voor BY-NC-SA). Er mag dus geen code van lindbergh-loader in de bronnen van
RavenGun, niet als kopie onder `third_party/`, niet als stukje in `src/`.

Wel toegestaan door de licentie, en wat RavenGun doet: de eigenaar haalt de bron zelf op en bouwt hem op zijn eigen machine voor eigen,
niet-commercieel gebruik; RavenGun verspreidt geen code en geen binaries van lindbergh-loader. Dat is hetzelfde patroon als de
OpenParrot-bouw (`third_party/openparrot/`), alleen om een andere reden (daar is de licentie verenigbaar, maar hoort de bron niet in
de repo). De laag is daarmee een **onderdeel van de app**, geen extern hulpmiddel: geen eigen starter, geen eigen UI, geen shellketen.
De daemon start de game zelf (`src/ravengun/launch/native.*`) en zet `lindbergh.so` in LD_PRELOAD; de starter `lindbergh` van de bron
wordt niet gebruikt (de engine doet zijn werk: LD_PRELOAD, LD_LIBRARY_PATH, `LINDBERGH_CONFIG_PATH`, zink).

In deze map staat alleen eigen werk plus één wijziging op de bron:

- `build-in-container.sh`: het bouwrecept (eigen werk, GPL-3.0-or-later).
- `patches/0001-passthrough-count-escaped-bytes.patch`: een wijziging op `src/lindbergh/passthrough.c` van de bron en daarom
  **CC BY-NC-SA 4.0**, zoals de bron (SPDX-regel bovenaan het bestand). Het is geen deel van de GPL-code van RavenGun; het bestand
  wordt alleen in de build-container op de bron toegepast. Bedoeld om upstream aan te bieden.
- `patches/0005-hod4-leaked-texture-units.patch`: a change to `src/lindbergh/patch.c` of the source, so also **CC BY-NC-SA 4.0**
  and not part of RavenGun's GPL code. HOTD4 Rev C only: after its multitexture path the game leaves texture units 1..N enabled
  (GL_REPLACE); on Mesa the in-game picture then turns uniformly white. The patch disables those units again at the end of that
  path (bench 2026-10-20; see the patch header and `docs/lindbergh.md`).

## Bouwen

    scripts/build-lindbergh-loader.sh              # bronnen ophalen, image maken als die ontbreekt, bouwen
    scripts/build-lindbergh-loader.sh --versions   # commits, basis-image en pakketversies tonen

Uitvoer: `~/.local/share/ravengun/lindbergh-loader/<commit>/` met `BUILDINFO.txt` (commits, sha256 van patch, recept en uitvoer,
pakketversies) en de verwijzing `~/.local/share/ravengun/lindbergh-loader/current`, die de daemon leest (`RAVENGUN_LINDBERGH` wijst een
andere map aan). Inhoud:

| Bestand | Herkomst | Waarom |
|---|---|---|
| `lindbergh.so`, `libsegaapi.so`, `libkswapapi.so`, `libposixtime.so` (+ `.so.1`, `.so.2.4`) | de bron, gebouwd met zijn Makefile | de laag zelf en de vervangers die de games als NEEDED hebben |
| `libSDL3.so.0`, `libSDL3_ttf.so.0`, `libSDL3_image.so.0` | SDL 3.4.16, SDL_ttf 3.2.2, SDL_image 3.4.6 (zlib), i386 | `lindbergh.so` linkt ertegen; SteamOS heeft geen i386-SDL3 |
| `libstdc++.so.5`, `libncurses.so.5`, `libtinfo.so.5`, `libXmu.so.6` | Ubuntu 22.04 i386 | NEEDED van HOTD4/HOTD4SP, Primeval Hunt en GSEvo; SteamOS heeft ze niet |
| `libCg.so` (2.0.0.12), `libopenal.so.0` | `libs/` van de bron | Cg-hercompilatie op Mesa (HOTD EX, Rambo, Too Spicy); OpenAL 0.0.8-ABI van Too Spicy |

FAudio 26.09 wordt statisch in `libsegaapi.so` gelinkt (zoals de Makefile van de bron wil).

## Toolchain (vastgepind)

`packaging/containers/lindbergh-loader/Containerfile`: `docker.io/library/ubuntu@sha256:5ec03bb3441e8b0bf3b4f9cd4629a1ae763010dc3035bb8da3ae6cf026486401`
(22.04) met pakketten uit `snapshot.ubuntu.com` van 2026-10-01 (alleen `ca-certificates` komt uit het gewone archief: zonder dat pakket
kan apt de momentopname over https niet lezen). Gebouwd met gcc 11 `-m32`, zonder netwerk. Bronnen (gecontroleerd op commit na het
ophalen, alleen `git fetch` en `git archive`): lindbergh-loader `0af606d8`, SDL `fa2c02bb` (release-3.4.16), SDL_ttf `a1ce3670`
(release-3.2.2), SDL_image `f661fa1a` (release-3.4.6), FAudio `667942a9` (26.09).

## Wat wij wijzigen

1. `0001-passthrough-count-escaped-bytes.patch`: bij JVS-doorgifte (`EMULATE_JVS = false`, de route van RavenGun) telt de leesdraad
   (`readJVSFrameThread`) de lengte van een antwoord in ruwe bytes: lengtebyte + 3. Een escape (`D0`, gevolgd door de waarde min 1) maakt
   het pakket op de lijn een byte langer, dus elk antwoord met `D0` of `E0` in de gegevens of de som werd afgekapt (de game kreeg een
   pakket zonder som). Bij een lightgun gebeurt dat vaak: een as van 0xD0xx is een plek op het scherm, en 1 op 128 sommen is `D0` of `E0`.
   De patch telt per escape een byte bij en leest de lengtebyte zonder teken. Het gedrag staat als model in `tests/test_jvs.cpp`
   (`lindbergh_reader`): een antwoord van ons bord met as 0xD0D0 komt zonder patch twee bytes te kort binnen, met patch heel.
