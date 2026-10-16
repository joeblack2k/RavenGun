# De opties en de memory card van een game (`patches/<Game>/options.json`)

Per game staat in `patches/<Game>/options.json`, naast `game.ini`, wat de Qt-app toont onder Properties > Options en
Properties > Memory card: de instelbare opties en de bestanden waarin de game zijn toestand bewaart (NVRAM, savegame,
highscores). De app leest het (`src/app/core/GameOptions.*`, `GameData::loadOptions`); de daemon leest het niet.

- **Lagen.** De gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/options.json`) vervangt het meegeleverde bestand als
  geheel; er wordt niets samengevoegd. Een kapot bestand (geen JSON, geen object) geeft die game geen opties, met de melding op
  het tabblad Options. De app valt dan niet stil terug op het meegeleverde bestand. Zonder bestand zijn er geen opties.
- **Profiel.** De mapnaam is het profiel (`LGI`, `Halo`). Er staat dus geen `id` en geen `tp_profile` in het bestand.
- **Status en route.** Die staan niet hier maar in `game.ini` (`[Status]`, `[Input]`). Het oude `status_en`,
  `recommended_route` en `effort` zijn weg.
- **Installatie.** De installatie neemt `options.json` mee (CMakeLists.txt, `tools/dev/patchlayout.py`).

Tot 2026-10-16 stonden alle games samen in `packaging/game-options.json`. `tools/dev/split-game-options.py` maakte er de losse
bestanden van (zie de kop van dat script voor wat er niet in mee is gegaan).

## Velden

| veld | betekenis |
|---|---|
| `title` | Naam van de game in de bibliotheek. |
| `nas_folder` | Mapnaam van de game op de NAS. De scan herkent een gamemap daaraan, ook zonder uitvoerbaar bestand. |
| `platform` | Platform in tekst (Properties > General). |
| `exe` | Het uitvoerbare bestand relatief aan de gamemap (`TC5/Binaries/Win64/x.exe`). Een toevoeging tussen haakjes (` (profile)`) wordt genegeerd. De app leidt er de gamemap uit af. |
| `options` | De lijst opties, zie hieronder. |
| `memory_card` | `{"items": [...], "notes_en": "..."}` (of alleen de lijst). |

Alles is optioneel. Onbekende velden (zoals `source` en `confidence`, herkomst van een gegeven) worden genegeerd.

### Een optie

`key` (uniek binnen de game; de app bewaart de waarde onder deze sleutel in `library.json`), `name_en`, `type` (`bool`, `int`, `enum`
of `string`), `values` (enum), `min`, `max`, `default`, `description_en`, `note_en` en `target`: waar de waarde heen gaat.

| `target.kind` | betekenis |
|---|---|
| `tp_profile_field` | Een veld van het TeknoParrot-profiel (`category` + `field`). De app schrijft het in `[Config]` van `game.ini` (gebruikerslaag), of bij een game zonder eigen profiel in `UserProfiles/<profiel>.xml`. TeknoParrotUi spiegelt de waarde naar `teknoparrot.ini` naast het gestarte exe als `[Categorie] Veld=waarde`. |
| `tp_profile_attr` | Een element op het hoogste niveau van het profiel-XML (`element`, bv. `WineRunnerPath`). |
| `launch_env` | Een omgevingsvariabele (`name`; `companion` = extra `K=V` dat meegaat als de waarde aan staat). |
| `game_config_file` | Een configuratiebestand van de game: `path`, `base`, `format`, `section`, `key`, `edit_outside_game`. |

`game_config_file`: `base` is `game_dir` (standaard; relatief aan de gamemap op de NAS of aan de lokale view), `view_dir`, `tp_dir`,
`home` of `native_layer` (de schrijflaag van de native route, alleen voor `rtdt_adjval`). `format`: `ini`, `ini_onoff` en
`ini_yesno` (ON/OFF of YES/NO als woorden), `tab_ini` (SLEUTEL, tab, WAARDE), `space_ini`, `colon` (`Sleutel: waarde`) en `rtdt_adjval`
(het controlesom-bewaarde instellingenbestand van Raw Thrills). Andere formaten (`csv`, `xml`, `colon_text`, `shell_arg`, ...) zijn
alleen-lezen. `edit_outside_game: false` betekent: verander het alleen in het TEST-menu van de game zelf. Een doel dat de app niet kent
of niet mag schrijven, staat als alleen-lezen in de UI met de reden.

Het schrijven gaat nooit naar de NAS: een bestand daar is "read-only (on NAS)"; een symlink in een lokale view wordt eerst een lokale
kopie. Dat staat los van dit bestand (`rg::checkWritable`).

De app mag ook kortere vormen lezen, voor handwerk en tests: `name`, `label`, `description`, doelen als tekst
(`tp:Categorie/Veld`, `env:VAR`, `ini:BESTAND#Sectie/Sleutel`, `cfg:BESTAND#Sleutel`) en `memory_card` als lijst.

### Een memory-cardbestand

| veld | betekenis |
|---|---|
| `id`, `path` | Bestand, map of glob (`*`, `?`), met `~`, `{placeholders}` en `{a,b}`-reeksen. |
| `base` | `game_dir` (standaard), `view_dir`, `tp_dir`, `home` of `wine_prefix` (nog niet ondersteund). Relatief aan `drive_c` van de Wine-prefix voor `wine_prefix`. |
| `kind` | `savegame`, `nvram`, `highscores`, `bookkeeping`, `credits`, `operator_settings`, `state`, `log` of `protected_data`. Het laatste wordt nooit gewist. |
| `description_en`, `erase_effect_en` | Uitleg voor de gebruiker. |
| `safe_to_erase` | `yes`: de game maakt het opnieuw met standaardwaarden, alleen voortgang, scores en tellers gaan weg. `no`: nodig om te starten of onderdeel van een beveiliging, nooit wissen uit de app. `unknown`: niet vastgesteld, eerst een kopie maken. |
| `confidence` | `high` (door een bevindingenbestand of een script van de game genoemd), `medium` (in de gamemap gezien, gedrag afgeleid) of `low`. Alleen voor mensen. |

Een bestand dat echt op de NAS staat wordt alleen getoond, ook als het item als lokaal is gemarkeerd (`"local": true`).

## Voorbeeld (Halo, ingekort)

```json
{
  "title": "HALO Fireteam Raven",
  "nas_folder": "Halo Fireteam",
  "platform": "Raw Thrills Linux PC Based (64-bit ELF)",
  "exe": "g7/halo/game",
  "options": [
    {
      "key": "general.input_api",
      "name_en": "Input API",
      "type": "enum",
      "values": ["DirectInput", "XInput", "RawInput"],
      "default": "RawInput",
      "target": {"kind": "tp_profile_field", "profile": "Halo", "category": "General", "field": "Input API"},
      "description_en": "TeknoParrot input backend. RawInput = evdev on Linux (needed for the RavenGun virtual gun)."
    }
  ],
  "memory_card": {
    "items": [
      {"id": "pmuser", "path": "g7/halo/pmuser", "base": "game_dir", "kind": "savegame",
       "description_en": "Persistent operator/user store of the Raw Thrills engine.",
       "safe_to_erase": "yes", "erase_effect_en": "Resets highscores, audits and operator adjustments to defaults."}
    ],
    "notes_en": "Operator adjustment definitions under data/prod/adj/*/defs are encrypted/binary in this dump."
  }
}
```

(Het echte bestand is door `tools/dev/split-game-options.py` met `indent 2` uitgeschreven, dus langer.)

## Opmerking: de native route leest de TeknoParrot-doelen niet

Voor een game met `"enabled": true` in `native.json` heeft de app wel opties die naar een TeknoParrot-profielveld of naar
`teknoparrot.ini` gaan. De native route start zonder TeknoParrot en leest die velden nooit; die opties doen daar niets.
