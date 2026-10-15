# Register per game (`[Launch] Registry`)

Soms heeft een game een registerwaarde in de Wine-prefix nodig die Wine of Proton niet zelf zet. Die waarde hoort bij
de game, dus het `.reg`-bestand staat in de map van die game, onder `data/` (dat gaat mee bij de installatie):

```ini
[Launch]
Registry = data/wmv-asyncreader.reg
```

Meer bestanden kan ook, met komma's ertussen. De gebruikerslaag (`~/.local/share/ravengun/patches/<Game>/`) gaat voor
de meegeleverde map, net als bij de andere bestanden van een game.

## Wat rg-direct doet

Vóór de JVS-pipe en de game leest rg-direct elk bestand in met

```
wine reg import Z:\...\<bestand>.reg /reg:32     (een 32-bits game; /reg:64 voor een 64-bits game)
```

Dat gebeurt in de prefix en met de Wine van de game. `/reg:32` schrijft `HKEY_CLASSES_ROOT\...` en
`HKEY_LOCAL_MACHINE\Software\...` in de 32-bits weergave (`Wow6432Node`), dus daar waar een 32-bits game leest.

Een markering `<prefix>/.ravengun-reg/<naam>.<sha256>.<bits>` houdt bij wat al is ingelezen. Een volgende start doet
dan niets ("is al ingelezen"). Een gewijzigd bestand krijgt een nieuwe hash en wordt opnieuw ingelezen. Lukt het
inlezen niet, dan start de game niet: een run zonder zijn instelling is een ongeldige run, net als bij de drop-ins.

**Let op:** de prefix is meestal de gedeelde (`prefixes/shared`). Een registerwaarde geldt daar dus ook voor de andere
games in die prefix. Kies een sleutel die zo smal mogelijk is (een bestandsextensie, niet een hele klasse) en leg in
het `.reg`-bestand zelf uit waarom hij er staat.

## Gemeten

**Operation G.H.O.S.T.** (2026-10-15), `patches/OG/data/wmv-asyncreader.reg`:

- `.wmv` gaat naar de gewone bestandsbron (AsyncReader) met de GStreamer-splitter van Wine, in plaats van naar de WM
  ASF-lezer (qasf).
- Met de ASF-lezer van Proton 11 bevroor het spel op het titelscherm, nadat de titelfilm klaar was. In de trace
  stopte de hoofdthread de graph (`MediaControl_Stop` → `WMReader_Stop`) en wachtte toen op `WMT_STOPPED`. De
  callback-thread wachtte op de audio-leesthread, en die hing in `BaseMemAllocator_GetBuffer`. Volgens de broncode van
  qasf (`asf_reader_cleanup_stream`) stopt de lezer eerst en geeft hij pas daarna de allocators vrij. De splitter
  (`parser_cleanup_stream` in winegstreamer) geeft de allocators eerst vrij.
- Met deze sleutel loopt het spel door de titel en de attract. Gespeeld: 5,5 min.
