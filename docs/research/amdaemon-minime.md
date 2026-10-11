# Is `minime` een vervanging van `amdaemon`? — nee, en het is iets anders

*Ronde 12 (2026-10-11). Alleen lezen. Dit beantwoordt vraag 2 van de opdracht. De bron staat op de
NAS in `findings/loaders/SegaTools/bin/minime/`; het Sega-shellprotocol in
`findings/loaders/SegaShell/`. Zie [`amdaemon-mechanisme.md`](amdaemon-mechanisme.md) voor wat
`amdaemon` zelf is.*

## Het korte antwoord

**Drie verschillende dingen, en ze zijn niet uitwisselbaar.**

| | Wat het is | Waar het staat | Waar het mee praat | Wat het levert |
|---|---|---|---|---|
| **`amdaemon.exe`** | Sega's AM Daemon, een **lokaal proces** naast de game | in de spelmap: `AMDaemon/` | de game, via **gedeeld geheugen en mutexen** (`amdaemon::ipc::`), en de ALL.Net-server via HTTP | de AM-diensten, de schijfletters, de keychipcontrole, het USB-IO-bord, de boekhouding |
| **`minime`** | een **server** voor ALL.Net en AiMe, in TypeScript op Node.js | `SegaTools/bin/minime/` | de **daemon** (en de game) over het netwerk | HTTP op 80 (`/sys/servlet/PowerOn`), AiMeDB op 22345, billing op 8443 (TLS), plus titelpoorten; een SQLite-database en een namaak-PKI |
| **`Shell.exe` + het `TFM`-protocol** | de Sega-**launcher** met een eigen TCP-server | `Sega/shell/` (de bundel van Transformers: Shadows Rising), niet in de Scarlet Dawn-map | de **game**, over TCP | kasttype (`SHELL_MSG_SET_SKU`) en het antwoord op de beveiligingsuitdaging (`SHELL_MSG_SECURITY_AUTHENTICATE`) |

`minime` vervangt dus de kant waar `amdaemon` **naartoe** belt. Het vervangt `amdaemon` niet, en het
vervangt het `TFM`-protocol van de Ring-titels niet.

## 1. Wat `minime` is

De **hele** README is één regel:

> `A simple self-hosted ALLNET/AiMe server written in TypeScript, running on Node.js, that stores its data in an SQLite database.`

`package.json`: naam `minime`, versie 0.1.0, `"description": "Test server"`, hoofd `src/index.js`,
afhankelijk van onder meer `express`, `raw-body`, `multiparty`, `iconv-lite`, `sql-bricks-postgres`
en `@decafcode/sqlite`. `start.bat` controleert of `node` en `npm` er zijn, doet zo nodig
`npm install`, zet `DEBUG=app:*` en start `node --tls-min-v1.0 .\bin\index.js`. Het is dus een
Node-programma dat je zelf draait, niet een Windows-binary naast de game.

De bronboom: `src/aimedb/` (het AiMe-lezerprotocol), `src/allnet.ts`, `src/billing.ts`,
`src/chunithm/`, `src/diva/`, `src/idz/` (userdb, ping, match), `src/sql/`, `src/switchboard.ts`. De
database heeft `schema/init/{aime,cm,idz,meta,util}.sql` plus migraties `M0007` t/m `M0018`. De
`CHANGELOG.md` gaat over Chunithm, Initial D Zero en Project Diva.

## 2. Welke poorten het opent, en welke niet

`src/switchboard.ts` (letterlijk):

```ts
export const PORT_AIMEDB = 22345;
export const PORT_ALLNET = 80;
export const PORT_BILLING = 8443;
export const PORT_CHUNITHM = 9001;
export const PORT_DIVA = 9000;
export const PORT_IDZ = {
  USERDB:    { TCP: 10000, HTTP: 10001 },
  MATCH:     { TCP: 10002, UDP_SEND: 10003, UDP_RECV: 10004 },
  TAG_MATCH: { TCP: 10005, UDP_SEND: 10006, UDP_RECV: 10007 },
  EVENT: 10008, SCREENSHOT: 10009, ECHO1: 10010, ECHO2: 10011,
};
```

en `src/index.ts` regel 44–57 bindt ze allemaal (`HOST_INT`, standaard `0.0.0.0`):
`net.createServer(aimedb).listen(22345)`, `http.createServer(allnet).listen(80)`,
`https.createServer(tls, billing).listen(8443)`, Chunithm 9001, Diva 9000, IDZ-userdb 10000 en de
ping-poorten.

**En de poorten van de Ring-titels zitten er niet in.** 40100, 40102, 40103, 40106, 40107, 40114 en
40115 komen in de hele `src/` niet voor; die horen bij de **PCPA**-client in de AM Library van de
Ring-games (`findings/antwoorden_poort40100.md`: `amMasterInit` zet `0x9CA4` = 40100, `amDongleInit`
40106/40107, `amInstallInit` 40102/40103, `amStorageInit` 40114/40115). `minime` dekt die laag niet
en hoeft dat ook niet: het is de server aan de andere kant van het netwerk.

## 3. Wat het protocol is

**ALL.Net** (`src/allnet.ts`): één route, `POST /sys/servlet/PowerOn`. Het lichaam is base64, dat
met zlib is ingepakt, en daarna `key=value&key=value` **zonder URL-escaping**; het antwoord is
dezelfde vorm plus een newline, gecodeerd als **Shift-JIS**. De velden in het antwoord zijn onder meer
`stat=1`, `uri`, `host`, `place_id`, `country`, `allnet_id`, `client_timezone=+0900`, `utc_time` en
`res_ver=3`.

**Billing** (`src/billing.ts`): het lichaam is `inflateRawSync`, er wordt een handtekening gemaakt met
`pki/billing.key`, en er staan constanten in als `playlimit = 1024` en `nearfull = 0x00010200` met de
opmerking *"Keychip odometer limit. Game will lock out if the odometer reaches…"*.

**AiMeDB** (`src/aimedb/`): een binair protocol over TCP op 22345. `decoder.ts` leest onder meer
`keychipId` uit de berichttekst (`msg.toString("ascii", 0x0014, 0x001f)`) — dat is het keychip-veld
van de **AiMe-lezer**, niet van een lokale dongle.

**De PKI** (`initpki`, een shellscript dat de gebruikte stappen documenteert): openssl genereert een
CA (`CN=DummyCA/O=DummyPKI`), een servercertificaat (`CN=ib.naominet.jp`) en een billing-sleutel.
Eén stap is veelzeggend: de CA wordt ook als DER weggeschreven met de opmerking *"Convert PEM cert to
DER form for emulated keychip. DER must fit in 1024 bytes so it must be small."* `minime` levert dus
ook het certificaat dat een nagebootste keychip aan de daemon voorlegt.

## 4. Wat `minime` **niet** doet

In de hele bron staan geen `amdaemon`, `AMFS`, `JVS`, `dongle`-driver, `usb`-IO of
hardwarecontrole. De enige treffers op "keychip" zijn het AiMeDB-veld hierboven. Concreet levert
`minime` niet:

* de **schijfletters** AMFS, D en Y (`TeknoParrot/{AMFS,D,Y}` in de spelmap);
* de **gedeelde-memory-diensten** waar de game mee praat (`amdaemon::ipc::`);
* de **JVS- en USB-IO-koppeling** (`config.json` eist een bord met `coin_input`, `switch_input`,
  `general_output` en `unique_io`);
* de **keychipcontrole** van de daemon (`amDongle*`, `\??\FddDriver`);
* de **hardwarecontrole** van `pxGetHwInfo.ini` (die overigens in deze keten door niemand wordt
  uitgevoerd);
* het `TFM`-shellprotocol van de Ring-titels.

En het kent **Scarlet Dawn niet eens**: `switchboard.ts` heeft een startlijst met alleen

```ts
startupHosts.set("SDDF", `${HOST_EXT}:${PORT_IDZ.USERDB.TCP}`);   // Initial D Zero
startupUris.set("SDBT", `http://${HOST_EXT}:${PORT_CHUNITHM}/`);  // Chunithm
startupUris.set("SBZV", `http://${HOST_EXT}:${PORT_DIVA}/`);      // Project Diva
```

Het spel-id van Scarlet Dawn is `SDET` (`99_StartApp.bat`: `set GAME_ID=SDET`). `startupHost("SDET")`
en `startupUri("SDET")` geven dus een lege string terug.

## 5. Is het nodig om Scarlet Dawn te starten? Nee

Uit de eerdere ronde (`findings/HOTDScarletDawn.md`): de game is **speelbaar via TeknoParrotUi**
zonder enige ALL.Net-server, en het daemonlog toont `amdaemonExe Ver.2437` en
`Process state: Advertise`. Dat is ook wat je verwacht: een arcadeautomaat in een kast zonder
netwerk hoort te blijven werken, en `amdaemon` heeft daar de functies voor
(`amMasterReconnectUsbDevice`, `Retry Keychip Access.`, `error.auto_reset = true` in `config.json`).

`minime` is dus voor Scarlet Dawn **niet nodig voor de start**. Het zou pas interessant worden als je
de boekhouding, de AiMe-kaartlezer of de facturatie wilt nadoen — en dan nog is het de vraag of deze
game die routes gebruikt, want zijn `amdaemon` heeft geen startlijst-vermelding voor `SDET`.

## 6. Het `TFM`-protocol van de Ring-titels (voor het onderscheid)

`findings/loaders/SegaShell/DOCS.md` en `BERICHTEN.md` beschrijven `Shell.exe` (1.438.720 bytes), de
Sega-launcher die een **eigen TCP-server** draait waar de game als cliënt naartoe belt. Het pakket:

```
54 46 4D 00 | 40 00 | 01 00 | 00 00 | 00 | 00
54 46 4D 00 | 42 00 | 04 00 | 00 00 | 01 | DE 55 15 AE
```

| Veld | Grootte | Betekenis |
|---|---|---|
| `54 46 4D 00` | 4 | magic `"TFM\0"` |
| `40 00` / `42 00` | 2 | berichtnummer (little-endian) |
| `01 00` / `04 00` | 2 | lengte van de inhoud |
| `00 00` | 2 | onbekend (in dit log altijd nul) |
| `00` / `01` | 1 | volgnummer |
| rest | *lengte* | de inhoud |

Uit `Shell.exe` komen **77 berichtnamen** (`BERICHTEN.md`); de berichten van de game naar de shell
gaan over starten (`GAME_MSG_LAUNCHED_OK`), het verloop (`GAME_MSG_STAGE_LOADING`), munten
(`GAME_MSG_CONTINUE_OFFERED`), scores en het pistool (`GAME_MSG_SHOT_FIRED`). En de game vraagt
ongeveer elke tien seconden om een nieuw antwoord op een beveiligingsuitdaging
(`GAME_MSG_SECURITY_CHALLENGE` → `SHELL_MSG_SECURITY_AUTHENTICATE` met vier wisselende bytes).

**Dit hoort niet bij Scarlet Dawn.** In de spelmap staat geen `Shell.exe`
(`find $G -maxdepth 3 -iname 'Shell.exe'` is leeg), de startketen noemt hem niet, en `Shell.exe` komt
uit `Sega/shell/` — de bundel waar ook `Sega/Transformers2/`, `Sega/ShellData/` en
`Sega/Launcher/SystemLauncher.exe` staan. Dat is **Transformers: Shadows Rising**, een RingEdge
2-titel, en de opdracht noemt het `TFM`-protocol dan ook "voor de Ring-titels". Welk poortnummer de
shell gebruikt staat **niet** in `DOCS.md`; daar staat alleen "de Sega-shell draait een eigen
TCP-server" (onbekend).

## 7. Samengevat

| Vraag | Antwoord |
|---|---|
| Is `minime` een vervanging van `amdaemon`? | **Nee.** Het is een ALL.Net/AiMe-**server**, geen daemon. Het draait niet naast de game, maakt geen schijfletters, levert geen gedeeld geheugen, doet geen JVS/USB-IO en controleert geen lokale dongle. |
| Is het een vervanging van de ALL.Net-server? | **Ja**, voor de titels die het kent (Chunithm, Initial D Zero, Project Diva, Card Maker, AiMe). Voor Scarlet Dawn (`SDET`) staat het niet in de startlijst. |
| Heeft Scarlet Dawn het nodig om te starten? | **Nee.** De game is speelbaar zonder, en de daemon hoort tegen een ontbrekend netwerk te kunnen. |
| Wat staat er dan nog tussen ons en een eigen route? | Niet `minime`, maar de twee blokkades uit [`docs/amdaemon-layer.md`](../amdaemon-layer.md): de twee-programma-start (lader-werk) en de daemon zelf (engine-werk). |
