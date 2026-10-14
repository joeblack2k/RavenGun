# Medaru No Gunman — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** new measurements; corrects a working assumption.
**Verdict:** two **separate** protections, and the one that produces the measured failure is **not** the
keychip. (1) A **Sega keychip dongle** through the `amDongle` API, reading
`../keychip/SBRZ_Table.dat` and `../keychip/SBTF_Table.dat`. (2) An **EEPROM/RAM-data** requirement, and the
`3001 RAM DATA IS BAD` / `3003 ROM HAS CHANGED` the game prints at every fresh start belongs to that second
one — `exe/eep.bin` is **0 bytes**. We answer **neither** yet, but the 1.19 MB tables are not a
one-key-away problem: they are encrypted per game.

---

## 1. What is present (MEASURED)

Sega **AM Library Ver.1.02, Build Oct 14 2009**, with **`amDongle Ver.1.03, Build Oct 14 2009`** and
`SEGA NN Library for DirectX G2.0`. PE32 i386. Profile `MedaruNoGunman.xml`: `TeknoParrot`,
`ExecutableName = gunman_dxF.exe`.

| File | Size | md5 |
|---|---|---|
| `exe/gunman_dxF.exe` (the one the profile runs) | 6823424 | `ffd6e3c06a2bf4a0abbe0961589432cc` |
| `exe/gunman_dxR.exe` | 10900992 | `12666f3917779bf2bda624224a2dd346` |
| `exe/gunman_dxD.exe` | 15405056 | `304bdb086204d6fa53eb65ad7073a2e0` |
| `exe/game.exe` | 121344 | `47ef2dc367e66d7d710cac0acdda4e8f` |
| `exe/game_test.exe` | 121856 | `f44dfe0c17100e04b48ee30ce9d67392` |
| `mxGetHwInfo.exe` | 185344 | `437ed95cf2dd89e6f889e3ed8d09212e` (its own build — not the 187904 or 202240 one) |
| `exe/eep.bin` | **0** | — |
| `exe/user_backup_errorlog.bin` | 0 | — |
| `exe/user_backup_main.bin` | 0 | — |
| `exe/user_backup_sate.bin` | 1304 | — |
| `exe/user_backup_sometime.bin` | 600 | — |
| `keychip/SBRZ_Table.dat` | 1190000 | `2a1dee6e9df0b0f0d3d2f87e8ff2444a` |
| `keychip/SBTF_Table.dat` | 1190000 | `1d0108046bcf762a283d022114c0790a` |

`mxGetHwInfo.ini` (1225 bytes, md5 `e939e671be431ef47eaeb99280ca6f4f`) is the **German-comment, Ver.0.050**
edition — every real check in it is commented out; after stripping comments only the three empty section
headers `[OS]`, `[System]`, `[Hardware]` remain. Compare LGS's Ver.1.07 ini, which is fully populated — so
Medaru's hardware probe is effectively disabled in the dump.

## 2. What it asks for (MEASURED)

### 2a. The keychip dongle

`gunman_dxF.exe` contains the whole `amDongle` client plus the keychip configuration namespace:

```
amDongleOpen / amDongleOpenEx / amDongleSetupKeychip / amDongleUpdate / amDongleBusy
amDongleSendAndRecvEx / amDongleResponseCheck
amDongle Ver.1.03 Build:Oct 14 2009 14:43:28
keychip.appboot.dvdflag / .formattype / .gameid / .modeltype / .networkaddr / .platformid / .region / .systemflag
keychip.billing.keyid / .mainid / .nearfull / .playcount / .playlimit
keychip.decrypt / keychip.encrypt / keychip.version
keychip.ds.compute / keychip.ssd.proof
keychip.tracedata.get / .put / .restore / .sectorerase
ERR_KEYCHIP / ERR_KEYCHIP_DATA / Error Response KeychipError !!! / Keychip Not Found.
KEYCHIP ID  %s   /   KEYCHIP ID  AAMM-01A00014501   /   KEYCHIP ID [%s]
```

That is the **same API and the same `keychip.*` key set** as HOTDSD's `amdaemon` draft descriptor — see
`patches/HOTDSD/dongle.sega-keychip.draft.json`. This is the single most useful thing in this note: Medaru and
HOTDSD are one work item, not two.

Region/EEPROM coupling, from the `CheckRegion_EepRom` family:

```
CheckRegion_EepRom:Dongle(gl) and Main(chn):failed
CheckRegion_EepRom:Dongle(gl) and Main(exp):success
CheckRegion_EepRom:Dongle(gl) and Main(jpn):failed
CheckRegion_EepRom:Dongle(gl) and Main(usa):failed
CheckRegion_EepRom:Dongle(jpn) and Main(jpn):success
CheckRegion_EepRom:dongle %d,main %d
CheckRegion_EepRom:unknown dongle:failed %d
```

So the dongle carries a region and the **Main EEPROM** carries a region, and the two must agree. This is a
third thing, distinct from both the keychip API and the RAM-data error.

### 2b. The two tables, and why they are not a shortcut

`gunman_dxF.exe` names **both** files, on adjacent lines:
```
../keychip/SBTF_Table.dat
../keychip/SBRZ_Table.dat
```
and both four-letter names appear alone as `SBRZ` / `SBTF`. Which one the game actually selects — by region,
by model, or by a fallback order — is **not determined** from these strings.

What the two tables are, measured:

* both exactly **1190000 bytes**;
* they differ in **1185284 of 1190000 bytes (99.6%)**, and the differences are spread evenly over the whole
  file (per-100000-byte-block counts 99562–99626, no quiet zone, no shared header) — they differ from **byte 0**;
* `gzip -9` output is **larger than the input** (1190218 > 1190000) for **all six** tables in the family
  (`SBRZ`, `SBTF`, `SBUH` LGI, `SBWB` LGI3D, `SDDV` TNXK0O~C, `SBYR` Transformers) — i.e. incompressible,
  high-entropy, encrypted or random;
* the first 512 bytes of each show no magic, no ASCII, no structure;
* calibration: `cmp -l` of `SBYR_Table.dat` against a copy of itself reports **0** differing bytes and exits 0,
  so the diff tool is honest; the pairwise matrix is symmetric and self-comparisons are 0.

**Conclusion (MEASURED structure, INFERRED meaning): these are per-game encrypted dongle images. There is no
shared blob and no derivable per-game key.** So one game's table cannot be produced from another's, and the
tables cannot be treated as a lookup table we can generate.

### 2c. The EEPROM/RAM-data requirement — the actual `3001 RAM DATA IS BAD` cause

`gunman_dxF.exe` carries a plain cabinet error-message list; the relevant adjacency is:

```
ROM HAS CHANGED
EEPROM INITIALIZE FAIL
RAM DATA IS BAD          <- the message the inventory records at every fresh start
NETWORK ERROR
```

Three things follow:

* `RAM DATA IS BAD` sits between `EEPROM INITIALIZE FAIL` and `NETWORK ERROR` — it is an **EEPROM/RAM** message,
  not a keychip message. There is no `ERR_KEYCHIP`-style message for it.
* **The `3001` in "3001 RAM DATA IS BAD" is not this message's code.** Every `3001` string in the binary belongs
  to **CRI/ADX audio**: `E4063001:Can't entry file`, `E2003043001 : Packet header parsing failed.`,
  `E2004113001 : Can not create critical section.`, `E2000092901`, `LSC/PCx86 Ver.2.30 Build:Nov 13 2008`. So the
  number and the text are two different things that got joined in our notes.
* `exe/eep.bin` is **0 bytes**, and so are `user_backup_errorlog.bin` and `user_backup_main.bin`, while
  `user_backup_sate.bin` (1304) and `user_backup_sometime.bin` (600) are not.

**INFERRED, and it is the correction this note exists for:** `3001 RAM DATA IS BAD` / `3003 ROM HAS CHANGED` is
the **EEPROM/SRAM path with a 0-byte `eep.bin`**, not the keychip. The recipe in
`docs/loskoppelen-per-game.md` row 19 groups them as "(SRAM/EEPROM-grootte)", which is consistent with this;
`INVENTARIS-dongle-per-game.md`'s status line for this game, which reads them next to a dongle story, is not.

Also present, and relevant to how the game was meant to be started — every `bat/*.bat` passes
**`e_Main_IsDongleDevelop=1`**:

```
gunman_r_main.bat          gunman_dxR.exe e_Main_MainUnit=1 e_Serial_Use=1  e_Main_IsRenge=1 e_Main_Window_FullScreen=1 e_Main_IsDongleDevelop=1
gunman_r_sate_noserial.bat gunman_dxR.exe e_Main_MainUnit=0 e_Serial_Use=0  e_Main_IsRenge=1 e_Main_Window_FullScreen=1 e_Main_IsDongleDevelop=1
gunman_r_pc_nocpk.bat      gunman_dxR.exe e_Main_MainUnit=0 e_Serial_Use=0  e_Main_IsRenge=0 ... (the PC variant; no dongle flag)
```

So the original launchers run with a **dongle-develop flag on**, in both the "main unit" and the "satellite"
shapes, and the only launcher without it is the PC/`nocpk` mode. Whatever `IsDongleDevelop` does to the dongle
path has been on in every cabinet-shaped start, and our route (`game.bat` → `exe/game.exe` → `gunman_dxF.exe`)
takes none of these arguments.

## 3. Do we already answer it?

**No.** `patches/MedaruNoGunman/` contains only `README.md` and `game.ini`. There is no `dongle.json`, no
keychip layer and nothing that touches `eep.bin`. The shared `dongles/` layer knows three types — `hasp-hl`,
`region-pcb` and `sega-scan` — and `sega-scan` is explicitly a framework with only stage 1 known. There is no
`sega-keychip` type at all, even though two of our games (this one and HOTDSD) need exactly that.

`docs/region-layer.md` §6 already names this gap: *"De keychip van Medaru No Gunman is een eigen laag
(facturatie, tracelading, SSD-bewijs)"*.

## 4. What is missing (and cost)

1. **The `sega-keychip` / `amDongle` answering layer — one piece for two games.** A hook on the keychip device
   (HOTDSD's draft names `\\.\FddDriver` and a `CreateFileW` + `DeviceIoControl` pair) that answers the
   `amDongle*` calls and supplies the `keychip.appboot.*` / `keychip.billing.*` values from a descriptor.
   **Cost: 2–4 days for a first shim plus a harness**, and HOTDSD's draft already fixes most of the field list.
   The two remaining unknowns in that descriptor (region/modeltype) are shared with HOTDSD.
2. **A `sega-keychip` descriptor type in `dongles/`** so both games hold only their own parameters, per the
   shared-material rule. **Cost: an hour of schema work** — HOTDSD already has
   `patches/HOTDSD/sega-keychip.schema-fragment.json` to start from.
3. **Which of `SBRZ` / `SBTF` the game wants**, and whether the other is a fallback. **Cost: static, a few
   hours** — disassemble around the two string references at offsets 9060/9061 and see which branch reads the
   filename.
4. **The EEPROM/RAM data.** Most likely a data fix rather than code: a non-empty `eep.bin`, or the game's own
   EEPROM-initialise path driven once. **Cost: hours, but its interaction with the `CheckRegion_EepRom` triple
   (dongle region vs Main region) has to be understood first.**
5. **The two 1.19 MB tables are probably a non-item.** If the `amDongle` layer answers the API, the game never
   needs the real dongle's encrypted image — the same reasoning that lets TeknoParrot run LGI/LGI3D/LGS with
   only `DongleRegion`/`PcbRegion`. This should be confirmed before anyone spends time on them.
6. **`e_Main_IsDongleDevelop=1`** — worth understanding, because it may be a supported developer path that
   makes the dongle question cheaper than an emulator. **Cost: static, an hour.**

## 5. Proposed test (NOT RUN) + positive control

**One test, in two arms, deliberately chosen to separate the two protections** — because conflating them is
what produced the wrong working assumption.

* Arm 1 (cheap, isolates the EEPROM): start the game with **no keychip layer at all**, but with the EEPROM gap
  closed — either a non-empty `eep.bin` in a view-local `exe/`, or the game's own EEPROM/RAM initialise driven
  once from the test menu.
* Arm 2: the same, **plus** the `amDongle` shim answering `amDongleOpen` / `amDongleIsAvailable` /
  `amDongleSetupKeychip` / the `keychip.appboot.*` reads.

* **Passing result (positive control).** Arm 1: a **fresh** start no longer prints `3001 RAM DATA IS BAD` or
  `3003 ROM HAS CHANGED`. That single result proves the RAM error is the EEPROM and not the keychip. Arm 2 then
  must additionally remove the dongle complaint — no `Keychip Not Found.`, no `ERR_KEYCHIP`, no
  `Error Response KeychipError !!!`.
* **Failing result (negative control).** A fresh start with today's 0-byte `eep.bin` and no shim **must** print
  `3001 RAM DATA IS BAD` / `3003 ROM HAS CHANGED` — that is the measured current state, and it is what makes
  Arm 1's silence meaningful. If that control does **not** reproduce the error, the error's cause is something
  else entirely and this whole section is wrong; that would be the most valuable possible outcome and must be
  reported as such.
* **A second, falsifiable prediction to check:** if the RAM error is *not* the EEPROM, then closing the EEPROM
  in Arm 1 will make `CheckRegion_EepRom:dll(...) and Main(...)` mismatch messages appear instead, because the
  Main region then comes from a freshly initialised EEPROM. Recording which of the two happens is the test.

## 6. Evidence log

```
# identity and build
strings -a -n 6 exe/gunman_dxF.exe | grep -i -E 'sega|AMLib|\(c\)|ver\.'
  -> Common_SEGALogo_ALL ; SEGA AM Library Ver.1.02 Build:Oct 14 2009 14:43:25 ;
     SEGA NN Library for DirectX G2.0 ; amDongle Ver.1.03 Build:Oct 14 2009 14:43:28

# the two tables, named by the binary
strings -a -n 4 exe/gunman_dxF.exe | grep -n -E 'SBRZ|SBTF|_Table\.dat'
  -> 9060:../keychip/SBTF_Table.dat
     9061:../keychip/SBRZ_Table.dat
     16920:SBRZ
     16921:SBTF

# the keychip API and keys
strings -a -n 4 exe/gunman_dxF.exe | grep -i keychip | sort -u
  -> amDongleSetupKeychip, ERR_KEYCHIP, ERR_KEYCHIP_DATA, Error Response KeychipError !!!,
     KEYCHIP ID  %s , KEYCHIP ID  AAMM-01A00014501 , KEYCHIP ID [%s] , Keychip Not Found.,
     keychip.appboot.{dvdflag,formattype,gameid,modeltype,networkaddr,platformid,region,systemflag},
     keychip.billing.{keyid,mainid,nearfull,playcount,playlimit}, keychip.{decrypt,ds.compute,encrypt,
     ssd.proof,version}, keychip.tracedata.{get,put,restore,sectorerase}

# the region/EEPROM coupling
strings -a -n 4 exe/gunman_dxF.exe | grep -i -E 'CheckRegion' 
  -> CheckRegion_EepRom:Dongle(gl) and Main(exp):success  (+ jpn/usa/chn failed variants,
     "dongle %d,main %d", "unknown dongle:failed %d")

# the RAM error is an EEPROM message, and 3001 belongs to CRI/ADX audio
strings -a -n 4 exe/gunman_dxF.exe | grep -n -B 25 -A 10 'RAM DATA IS BAD'
  -> ... SERIAL COM1..COM3 ERROR1..9 ; ROM HAS CHANGED ; EEPROM INITIALIZE FAIL ; RAM DATA IS BAD ; NETWORK ERROR
strings -a -n 4 exe/gunman_dxF.exe | grep -n -B 6 -A 3 '3001'
  -> E4063001:Can't entry file ; E2003043001 : Packet header parsing failed. ;
     E2004113001 : Can not create critical section. ; E2000092901 ; LSC/PCx86 Ver.2.30 Build:Nov 13 2008

# the 1.19 MB tables are per-game encrypted blobs, not one blob plus a key
md5sum + stat: all six *_Table.dat in the share are exactly 1190000 bytes
cmp -l SBRZ SBTF | wc -l                     -> 1185284   (99.6% of the file)
cmp -l SBYR copy-of-SBYR | wc -l             -> 0          (calibration: the tool is honest)
cmp -l SBRZ SBTF | awk '{b=int(($1-1)/100000); c[b]++} END{for(i in c) print i, c[i]}'
  -> blocks 0..11 = 99589 99590 99585 99597 99592 99617 99619 99562 99626 99626 99604 89677
cmp -l SBRZ SBTF | head -3                   -> offsets 1,2,3 already differ (no shared header)
xxd -l 512 SBRZ / SBTF / ...                 -> no magic, no ASCII, no structure
gzip -9 -c <each> | wc -c                    -> 1190218 for every one (larger than the input => incompressible)

# EEPROM state
ls -la exe/  -> eep.bin 0 ; user_backup_errorlog.bin 0 ; user_backup_main.bin 0 ;
                user_backup_sate.bin 1304 ; user_backup_sometime.bin 600

# the cabinet start scripts all run with the dongle-develop flag
cat bat/gunman_r_main.bat           -> gunman_dxR.exe ... e_Main_IsDongleDevelop=1
cat bat/gunman_r_sate_noserial.bat  -> gunman_dxR.exe ... e_Main_IsDongleDevelop=1
cat bat/gunman_r_pc_nocpk.bat       -> gunman_dxR.exe ... (no e_Main_IsDongleDevelop)
grep -v -E '^\s*;|^\s*$' mxGetHwInfo.ini   -> [OS] [System] [Hardware]   (the Ver.0.050 edition;
                                              every real check is commented out)
```

**Not measured, and not claimed:** which of `SBRZ`/`SBTF` the game selects and under what condition; whether
`e_Main_IsDongleDevelop=1` weakens or bypasses the dongle; whether the 0-byte `eep.bin` is the sole cause of
`RAM DATA IS BAD` (that is the INFERRED step, and §5 is designed to falsify it).
