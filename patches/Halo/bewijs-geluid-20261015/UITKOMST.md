# Halo native met geluid (STEREO / LPCM 5.1) -- uitkomst 2026-10-15

Route: eigen lader (native). Geluid via de FMOD-wrapper (tools/fmod-bridge): libfmod.so.10/libfmodstudio.so.10 in het spel,
rg-fmod-host.exe (data/fmod/) onder Wine met de echte FMOD Studio 1.10.2 uit de TeknoParrot-installatie van de gebruiker.
Keuze in de app: opties van de game, "Audio output" = STEREO of LPCM 5.1 ([Audio] Speakers in game.ini; standaard LPCM 5.1).

## Gemeten (de stroom van rg-fmod-host.exe zelf, parec --monitor-stream, float32, 8 kanalen)

De tv-sink is `hdmi-surround71` (8 kanalen, alle kanalen 100%, niet gedempt). Wine ziet "Speakers (PulseAudio Output)" als 7.1.

| | FL | FR | midden | LFE | achter (RL/RR) | zijkant (SL/SR) |
|---|---|---|---|---|---|---|
| zonder upmix, spel (FMOD in 5.1) | -19.2 | -27.0 | stil | stil | stil | stil |
| LPCM 5.1, spel na 40 s (20 s) | -18.5 | -23.4 | -24.7 | -24.9 | -35.8 | -30.3 |
| LPCM 5.1, spel na 3 min (30 s) | -19.8 | -24.9 | -26.4 | -26.9 | -36.7 | -31.2 |
| STEREO, spel (20 s) | -19.1 | -25.3 | stil | stil | stil | stil |

(RMS in dBFS.) De mix van Halo zelf is stereo; de 5.1 komt uit de upmix van de wrapper (midden = som, LFE = som < 120 Hz, achter =
verschil, 15 ms, < 7 kHz). FMOD verdeelt de 5.1-surround over de zij- en achterkanalen van het 7.1-apparaat. De keuze komt aan:
game.ini -> daemon (`RG_FMOD_SPEAKERS 5.1` / `stereo` in het startcommando) -> wrapper ("uitvoer LPCM 5.1" / "uitvoer stereo").

Spelen: 5 minuten Story Mode met LPCM 5.1 (contact-lpcm51.jpg), plus 90 s met STEREO.

## De keuze in de app

Properties van HALO Fireteam Raven -> Options -> Launch -> "Audio output": STEREO of LPCM 5.1 (app-opties-audio-output.jpg,
app-keuze-stereo-lpcm51.jpg). Gemeten via de app zelf: STEREO kiezen + Apply schreef `[Audio] Speakers = STEREO` in de gebruikerslaag
(~/.local/share/ravengun/patches/Halo/game.ini); terug naar LPCM 5.1 + Apply schreef `Speakers = LPCM 5.1` ("Saved. The new settings
apply the next time the game starts.", app-opgeslagen.jpg). De daemon zet die waarde bij de start om in `RG_FMOD_SPEAKERS`
(gemeten in het startcommando: `stereo` en `5.1`).

## Open

- De muziek en wapens van het spel zelf pieken af en toe boven 0 dBFS op FL (enkele samples per 20-30 s; in de attract-muziek meer).
  Dat zit in de mix van het spel; de wrapper verandert FL/FR niet.
- De attract-demo is soms stil: dat is waarschijnlijk de operatorinstelling voor attract-geluid (attractSoundFreqs), niet de wrapper.

Alle opnames: ~/.local/share/ravengun/bench-captures/Halo/2026-10-15-geluid/
