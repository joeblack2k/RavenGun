# Native DirectX-DLL's uit Steam (`[Launch] NativeDlls`)

Wine levert eigen versies van XACT, XAudio2, D3DX9/10/11 en d3dcompiler. Soms schieten die tekort: een HLSL-effect dat niet
compileert, of een geluidsengine die crasht. De echte DLL's van Microsoft staan al op elke machine met Steam, in
`Steamworks Shared/_CommonRedist/DirectX/Jun2010` (de DirectX-herdistribueerbare die Steam zelf installeert).

```ini
[Launch]
NativeDlls = xactengine3_3, d3dx9_40
```

## Klaarzetten: `scripts/setup-redist.sh`

Het script pakt alle DLL's uit de `*_x86.cab` en `*_x64.cab` van die map uit naar `~/.local/share/ravengun/redist/{x86,x64}/`
(met 7z, of anders bsdtar). Er wordt niets gedownload en niets komt in git: het blijven Microsoft-bestanden, alleen in de
gebruikersmap. `install-user.sh` draait het script één keer.

## Wat rg-direct dan doet

Per DLL:

- De native versie gaat naar `system32` (64 bits) of `syswow64` (32 bits) van de prefix van de game. Een COM-DLL zoals XACT laadt
  Wine altijd vanaf die plek, nooit uit de map van het spel.
- Wine's eigen versie wordt één keer bewaard als `<naam>.dll.wine-bewaard`.
- `WINEDLLOVERRIDES` krijgt `<naam>=n`.

Zonder die override laadt Wine zijn ingebouwde versie, dus andere games in de gedeelde prefix merken niets. Staat de DLL niet
in `redist/`, dan start de game niet. De melding noemt het script.

## Gemeten

**Operation G.H.O.S.T.** (2026-10-15):

- Met Wine's `xactengine3_3` viel het spel na het geluidscontrolescherm, met een access violation op `0x7142A0F3` in een
  XACT-thread.
- Met de native XACT 3.3 (`Nov2008_XACT_x86.cab`) loopt het door tot de attract (FREE PLAY).
- Na de attract volgt nog een tweede crash op `0x0051CFCC` in `gs2.exe`. Die is open.
