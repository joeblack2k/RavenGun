# COM1-harness voor Luigi's Mansion Arcade

Doel: meten **wat het spel werkelijk over COM1 doet**, en daarmee de open vraag uit
`docs/research/teknoparrot-io.md` (regel 108) beantwoorden: *"Luigi: het protocol na de
flash. Meting: een pty op COM1 en de bytes van `SendFlash` en de game loggen."*

Dit is **geen** manier om een controle te omzeilen. We geven het spel een echte seriele
poort in plaats van een poort die `ACCESS_DENIED` geeft, en kijken wat het doet. Een
"antwoord" op de poort hoort in de vorm van de I/O-kaart te zijn, niet in de vorm van
"de controle is geslaagd".

## De bestanden

| bestand | wat het doet |
|---|---|
| `luigi-com1-pty.py` | maakt een pty, zet de slave achter een vaste naam, logt elke byte met tijdstempel (hex + ascii), kan antwoorden |
| `luigi-com1-run.sh` | verwarmt de prefix, zet `dosdevices/com1` op de pty, draait `SendFlash.exe` of `VACUUM.exe`, zet com1 terug, vat samen |
| `luigi-com1-selftest.sh` | bewijst zonder Wine/prefix/game dat de pty, de link, het loggen en het antwoorden werken |

## Uitvoeren

**Stap 1 â€” bewijs eerst dat de opstelling werkt (5 s, geen Wine, geen scherm, geen bench):**

```bash
bash ~/projects/ravengun-patches/patches/LuigisMansion/com1-harness/luigi-com1-selftest.sh
```

**Stap 2 â€” het flitsprotocol, zonder het spel (dit is de goedkope meting):**

```bash
bash ~/projects/ravengun-patches/patches/LuigisMansion/com1-harness/luigi-com1-run.sh sendflash
```

`SendFlash.exe COM1 LMAIO.bin` is het gereedschap uit `game.bat`. Het opent COM1, zet de
DCB, wist de buffer en schrijft 140.688 B firmware. Je krijgt het hele flitsprotocol in
beeld: blokgroottes, de volgorde van `FLASH ERASE`/`FLASH WRITING`, en vooral **of het op
antwoord wacht voordat het verdergaat**. Dit raakt geen board (de pty is van ons).

Wil je zien wat er gebeurt als er wel geantwoord wordt:

```bash
RG_COM_REPLY_HEX="06" bash .../luigi-com1-run.sh sendflash      # na elk blok een ACK
RG_COM_ECHO=1         bash .../luigi-com1-run.sh sendflash      # alles terug
```

Vergelijk de drie logs: geen antwoord / ACK / echo. Waar het programma stopt verschilt,
en dat verschil *is* het protocol.

**Stap 3 â€” de game zelf (alleen als de bench vrij is; ~45 s laden):**

```bash
bash ~/projects/ravengun-patches/patches/LuigisMansion/luigi-preflight.sh
RG_RUN_MAX=60 bash ~/projects/ravengun-patches/patches/LuigisMansion/com1-harness/luigi-com1-run.sh game
```

**Let op:** met OpenParrot-patch 0005 weigert onze kern elke naam die met `COM` begint
voor `GameID::LuigisMansion`. Dan komt de game nooit bij de pty en meet je niets. Bouw
voor deze meting een kern **zonder 0005**:

```bash
mv ~/projects/ravengun/third_party/openparrot/patches/0005-luigismansion-com-block.patch /tmp/
bash ~/projects/ravengun/scripts/build-openparrot.sh
# ... meten ...
mv /tmp/0005-luigismansion-com-block.patch ~/projects/ravengun/third_party/openparrot/patches/
bash ~/projects/ravengun/scripts/build-openparrot.sh
```

Zet in het log/de notitie welke kern je gebruikte (`sha256sum` van `OpenParrot64.dll`).

## Waarom een pty en niet `/dev/ttyS0`

Gemeten op de Deck: `dosdevices/com1 -> /dev/ttyS0`, en die is `root:uucp 0660` terwijl
`deck` niet in `uucp` zit. Wine geeft de game dus `ERROR_ACCESS_DENIED` (5) in plaats van
"bestaat niet" (2). Dat verschil is precies wat patch 0005 wilde wegnemen, en het verschil
tussen 2 en 5 is een reeel gegeven over deze game: hij reageert er anders op. Met een pty
van ons krijgt hij een poort die wel werkt, en zien we wat hij dan doet.

Een pty kent `TIOCMGET` niet, dus `GetCommModemStatus` faalt met `ENOTTY`. Daarvoor is
`build/librg_comm_shim.so` (LD_PRELOAD); `luigi-com1-run.sh` zet die automatisch als hij
bestaat. Zonder die hulp meldt Wine een fout op de modemstatus en dat is een meting aan de
verkeerde laag.

## Twee valkuilen die al zijn opgelost

1. **Wine gooit `com1` terug.** Bij het starten van wineserver/wineboot zet Wine
   `dosdevices/com1..com32` opnieuw op `/dev/ttyS*`. Daarom eerst de wineserver
   voorverwarmen (`luigi-com1-run.sh` doet dat, net als `scripts/play-halo.sh`), dan pas
   de link zetten, en hem daarna terugzetten zoals hij was.
2. **`systemd-logind` en de ssh-sessie.** Een lang spel in een ssh-sessie kan bij het
   sluiten van de sessie sneuvelen; draai zo'n meting in een systemd-user-unit als het lang
   duurt.

## Wat het log zegt

Per regel: `tijd`, `mono` (seconden sinds de start), richting (`<<< spel schrijft`,
`>>> wij antwoorden`), de bytes in hex, en dezelfde bytes als leesbare ascii. Onderaan een
samenvatting. **Geen verkeer op COM1 is zelf een uitkomst:** dan raakte het programma de
poort niet aan, en is COM1 niet waar het op stukloopt.

