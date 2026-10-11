# _template — kopieer deze map om een game toe te voegen

Kopieer deze map naar `patches/<JouwGame>/` en werk de bestanden bij. Zie
`docs/een-game-toevoegen.md` voor de stappen en `docs/dropins.md` voor het contract.

Wat je mag weglaten:

- `dongle.json` + `dongle/` — als de game **geen** dongle vraagt. Aanwezigheid van `dongle.json`
  betekent "deze game vraagt een dongle".
- `dropin/` — als de game geen bestanden van ons nodig heeft.
- `patches.ini` — als de game geen bytepatches nodig heeft.

Wat je nooit mag weglaten: `game.ini` en deze `README.md`. Schrijf in de README **wat je hebt
gemeten en waarom** — dat is voor de volgende persoon meer waard dan de instellingen zelf.
