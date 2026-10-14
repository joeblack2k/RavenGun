# DXVK per game (`[Launch] Dxvk`)

Wine tekent Direct3D 8 tot en met 11 standaard met wined3d (OpenGL). Sommige games tekenen daarmee niet goed. DXVK doet
hetzelfde via Vulkan, en is wat Proton standaard gebruikt. RavenGun zet DXVK **per game** aan, zonder iets aan de gedeelde
prefix te veranderen:

```ini
[Launch]
Dxvk = true
```

## Wat rg-direct dan doet

- Het plan zoekt DXVK in de bitbreedte van de game (`Is64Bit` van het profiel: `x64` of `x32`).
- Daarna zet het `d3d8.dll`, `d3d9.dll`, `d3d10core.dll`, `d3d11.dll` en `dxgi.dll` als drop-in naast de exe in de view, met
  `WINEDLLOVERRIDES <naam>=n`.
- Het spel laadt ze uit zijn eigen map. Wine's `d3d10.dll` en `d3dx10/d3dx9` blijven de ingebouwde: die bouwen op
  `d3d10core`/`d3d9` van DXVK.
- Een eigen drop-in met dezelfde naam (`patches/<Game>/dropin/`, of de gebruikerslaag) wint van DXVK.
- De optie vraagt een view (`[Launch] View`): de DLL's komen nooit op de NAS.
- In het logboek staat `drop-in: dxvk: <map> uit <bron>`. DXVK zelf meldt `info:  DXVK: v...` in het logboek van de start.

## Waar DXVK vandaan komt

De eerste treffer wint:

1. `RAVENGUN_DXVK`: een map met `x32/` en `x64/`.
2. `~/.local/share/ravengun/dxvk/current`: `scripts/fetch-dxvk.sh` haalt de officiële release op
   (github.com/doitsujin/dxvk, zlib-licentie), controleert de SHA-256 en pakt hem daar uit.
3. De nieuwste DXVK uit een Proton van Steam. Gezocht wordt in
   `~/.local/share/Steam/compatibilitytools.d/*/files/lib/wine/dxvk/` en `steamapps/common/Proton*/files/lib/wine/dxvk/`.
   "Nieuwste" volgt de versie in het `version`-bestand van DXVK, niet de mapnaam.

Vindt het plan niets, dan start de game niet. De melding noemt `scripts/fetch-dxvk.sh`.

## Gemeten

**Mario Kart Arcade GP DX** (2026-10-15).

Met wined3d:

- ontbrak de hele 2D-laag: geen tekst, geen menu's, geen "FREE PLAY";
- gaf het logboek 144.000 keer `GL_INVALID_OPERATION in glDrawArraysInstanced`;
- werd het beeld na een knop zwart, terwijl het spel doorliep.

Met DXVK v2.7.1 (`info:  DXVK: v2.7.1-509`, feature level 12_1) staan menu's, kaartvraag, personagekeuze en de race in beeld.
