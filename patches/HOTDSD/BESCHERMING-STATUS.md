# House of the Dead: Scarlet Dawn (HOTDSD) — protection status

**Lane:** licence/hardware emulation. **Date:** 2026-10-14.
**Status of this note:** draft assessment. **The draft is NOT complete** — 7 `TODO` fields plus 2 empty
containers — but it is **adoptable as the skeleton of the shared layer**, and it is **not required for reach**.
**Verdict:** the keychip is a **Sega ALLS keychip** reached through `\\.\FddDriver` with a
`CreateFileW` + `DeviceIoControl` funnel. The daemon logs `Keychip Not Found` and **retries**; the game is
measurably playable with no keychip at all. So this is completeness work, not a start blocker — and almost every
open field needs a **run** (`hwvalue.exe` / a relay trace), not more reading.

---

## 1. What is present (MEASURED)

Two executables (profile `HOTDSD.xml`: `Hodzero-Win64-Shipping.exe` + `amdaemon.exe`,
`LaunchSecondExecutableFirst = true`). Completeness check from `patches/HOTDSD/measurements/completeness.txt`:

```
files            : 2081
total bytes      : 13095511155  (12.20 GB)
empty files      : 0
App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe      PRESENT 51.62 MB
App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping_org.exe  PRESENT 51.47 MB (?)  51470336
App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe                                  PRESENT  7.89 MB  8272384
App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe                                   PRESENT  202240
32 PE files ; truncated PE files: 0 []
Hodzero-Win64-Shipping.exe  imports=50  system=50  shipped-locally=0  UNRESOLVED=0
amdaemon.exe                imports=23  system=23  shipped-locally=0  UNRESOLVED=0
```

Notable: **`Hodzero-Win64-Shipping_org.exe` (51470336) sits beside `Hodzero-Win64-Shipping.exe` (54128128)** —
a 2.6 MB difference, i.e. the shipping binary has already been modified by someone. Any measurement should record
which one it ran. `App/Hodzero/WindowsNoEditor/AMDaemon/TeknoParrot/D/SDET` (8.00 MB) and
`AMDaemon/TeknoParrot/AMFS/sysfile.dat` (24576) are the AMFS/system-file side. The AiMe reader is
`AMDaemon/aime_firm/` (6 files) with `hid.dll`/`setupapi`/`cfgmgr32` imports.

## 2. What it asks for (MEASURED, from the existing probes)

`patches/HOTDSD/measurements/05-keychip-probe.txt` — 82012 ASCII strings in `amdaemon.exe` — gives the layer
with addresses, which is a stronger form of evidence than a string list:

```
  0x0058f8f0  DongleManager Build:Apr 19 2018 23:12:08 ($Rev: 2207 $)
  0x00596820  DongleContext Build:Apr 19 2018 23:12:08 ($Rev: 1166 $)
  0x005a3440  Error amDongleInit!!                       0x005a3510  Error amDongleExit!!
  0x005a3a40  amDongle Build:Mar 19 2018 10:41:25 ($Rev: 6676 $)
  0x005a3a78  Error: amyDongleOpen() failed.             0x005a3ab8 ...
  0x005a3b48  Error: amyDongleIsAvailable() %d.          0x005a3b70  Error: amyDongleGetGeometryNvram() %d.
  0x005a3b98  Error: amyDongleGetBillingMode() %d.       0x005a3c10  Function "amDongleSetAuthConfig" is not implemented.
  0x005a3e58  amDongleSetIv   0x005a3e90  amDongleDecrypt   0x005a3ec8  amDongleEncrypt
  0x005a3f00  Error: amsSystemPropertyGetFormatTypeOnDongle() failed %d.
  0x005a3fd0  Error: amsSystemPropertyGetPlatformIdOnDongle() failed %d.
  0x005a4250  amDongleReadNvramInternal   0x005a4380  Error: amyDongleReadNvram() failed %d.
  0x005a43b0  amDongleWriteNvramInternal  0x005a43d0  Error: amyDongleWriteNvram() failed %d.
  0x005a4400  amDongleGetBillingMode      0x005a4418  amDongleBillingGetKeychipId
  0x005a47c8  Error: amyDongleBillingGetSignaturePubKey() failed %d.
  0x005a4830  Error: amyDongleBillingGetCaCertification() failed %d.
```

From `REPORT.md` §9 (all marked **[M]** there), and confirmed by the probe file above:

* **Device path** `\??\FddDriver`, opened through the daemon's `\\.\%s` pattern as `\\.\FddDriver`.
* **Driver client layer** module `amyDongle` — `amyDongleDriverStart`, `amyDongleDriverEnd`,
  `amyDongleDriverStartNvram`, `amyDongleDriverEndNvram`, `amyDongleDriverStartGetLog`,
  `amyDongleDriverEndGetLog`, and the funnel `amyDriverSyncronizeDeviceIoControl`.
* **The actual I/O call** is `DeviceIoControl`, with errors `Error: DeviceIoControl failed. %08x`,
  `Error: DeviceControl failed. %08x`, `Error: Maybe library bug.`
* **Everything funnels into `DeviceIoControl` on the `\\.\FddDriver` handle**, so
  `CreateFileW` + `DeviceIoControl` interception in the daemon process covers the entire keychip surface —
  and **there is no kernel driver to write**: `FddDriver` is a SEGA-shipped component that does not exist under
  Wine, and we answer in user mode.
* **Identity** `amDongleGet{ModelType,Region,PlatformId,FormatType,GameId,SystemFlag,NetworkAddress}`.
* **NVRAM** `amDongleReadNvramInternal` / `amDongleWriteNvramInternal` / `amyDongleGetGeometryNvram`, and the
  daemon **validates every request itself**: `Nvram(%hhu) is out of range. Valid range %hhu - %u`,
  `Adress(%u) is out of range. Valid range 0 - %u`, `BufferSize(%u) is out of range. Valid range 1 - %u`.
* **Billing / ALL.Net** the `amDongleBilling*` family, and `BillingUpdatePlaylimit pKeychipId(pPlaylimit or
  pSign is NULL)` — **the play limit is signed**.
* **Errors** `Keychip Not Found`, `Keychip Access Failed`, `ERROR: invalid KeychipID(%s)`,
  `Error : Invalid Keychip ID : %s`, `Retry Keychip Access.`, `Error : keychip mode is wrong.`,
  `Disconnect dongle.` — *the daemon logs and retries; it does not abort.*
* **A registry contract**: `keychipId`, `platformId`, `formatType` under
  `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip`, read by `amsSystemPropertyGetKeychipId`,
  `amsSystemPropertyGetPlatformIdOnDongle`, `amsSystemPropertyGetFormatTypeOnDongle`,
  `amsSystemPropertyGetNetworkAddressIpv6`. **This is the cheap path**: if the registry already satisfies those
  readers, the expensive half of the layer may not be needed for a start.
* **The unit-test harness**: `hwvalue.exe` (202240 bytes, in `AMDaemon/`) links the same `amyDongle`/`amPlatform`
  client and prints `modeltype`, `region` and `dipsw` as plain text — the cheapest way to learn the right
  region/modeltype instead of guessing, and the acceptance test for any shim.

Also measured (`patches/LOADERS-ZES-GAMES.md`): the address `0x1402b4270`
(`amDongleIsAvailableInternal`) holds `cc cc cc cc` in this build — a patched-out function. That matters for
where a shim can attach.

## 3. Do we already answer it?

**No, and it is not needed to reach the game.** `patches/HOTDSD/README.md`/`VOLLEDIGHEID.md` and the draft's own
note all state the same measured fact: **HOTDSD is playable with no keychip at all** under TeknoParrot with
SEGA's unmodified daemon; the daemon logs `Keychip Not Found` and retries rather than aborting. The inventory
records HOTDSD as **playable** (start key 1, crosshair measured on angles and centre, trigger, sound).

`dongles/` has no `sega-keychip` type. `patches/HOTDSD/sega-keychip.schema-fragment.json` is a ready-to-merge
addition that would make it the fourth type, with a five-step adoption list. That fragment is the piece of this
work with the widest reach: it is the same type Medaru No Gunman, LGI, LGI3D, LGS, `OG` and `TNXK0O~C` all need.

## 4. What is missing (and cost)

**Adoptability verdict: the draft is a well-founded skeleton, NOT complete. Do not adopt it as
`patches/HOTDSD/dongle.json` yet** — but adopt the **schema fragment**, because the type is needed regardless of
this game.

### 7. Draft completeness — the open fields, and what closes each

| # | Field | State | What closes it | Does the value exist on disk? |
|---|---|---|---|---|
| 1 | `identity.modeltype` | placeholder `1`, note says TODO | run `hwvalue.exe modeltype` on an ALLS UX cabinet / seeded prefix | **No** — needs a run |
| 2 | `identity.region` | placeholder `4` (EXP), **INFERRED** from `config.json language=english` | run `hwvalue.exe region` | **No** — needs a run |
| 3 | `identity.formatType` | `0`, "not yet measured" | read `amsSystemPropertyGetFormatTypeOnDongle`'s source value (registry key `formatType`) | **No** — needs a run or a registry seed |
| 4 | `billing.keychipId` | literal `"TODO-20-chars"` | `amDongleBillingGetKeychipId` / `alpbGetKeychipSerialID` | **Partly** — `REPORT.md` §10's worked example carries `"A0000000000000000000"` (20 chars) as the *target shape*, not as a measured ALLS UX value. Do not promote it as measured. |
| 5 | `billing.mode` | `0`, "not yet measured" | `amDongleGetBillingMode` | **No** |
| 6 | `nvram.geometry` | literal `"TODO: from amyDongleGetGeometryNvram"` | `amyDongleGetGeometryNvram`, and it must be **consistent with the files** or the daemon rejects the read before the device sees it | **Partly** — `REPORT.md` §10's example shows `[{ "index": 0, "size": 4096 }]`, again a target shape, not a measurement |
| 7 | `nvram.files` | `{}` — empty | the NVRAM images themselves, one per index | **No** |
| 8 | `crypto.iv` | `"TODO"` | `amDongleSetIv` | **No** |
| 9 | `ioctls` | `[]` — empty | **measurement C2**, spelled out in `REPORT.md` §6: trace the daemon alone (no game) and collect the codes | **No** — C2 is *planned*, not run. The method is ready: `grep -a 'DeviceIoControl' "$V/amdaemon-relay.log"`, `grep -aoE 'code=0x[0-9a-f]+' … \| sort \| uniq -c`, with a documented fallback to `NtDeviceIoControlFile` |

So: **all 9 open items need a run, not more reading.** Nothing here is blocked on missing information — the
method for each is written down in `REPORT.md`. That is the useful conclusion.

What is already *complete and trustworthy* in the draft, and should not be re-derived:

* `device = "\\\\.\\FddDriver"` — MEASURED;
* `identity.platformId = "ACA0"` (ALLS UX, from `pxGetHwInfo.ini`) — MEASURED;
* `identity.gameId = "SDET"` (`99_StartApp.bat`) — MEASURED;
* `identity.networkAddress = "10.0.0.2"`, `systemFlag = 0`, `dipsw.value = 0` (with the known bit 4 = Terminal);
* `billing.playlimit = 1024` and `nearfull = 66048` (0x00010200), both from the open `minime/src/billing.ts`
  convention rather than from the cabinet — worth labelling as such;
* `billing.signaturePubKey = "pki/billing.pub"` / `caCertification = "pki/ca.crt"` with `minime/initpki`
  guidance (CA as DER, budget 1024 bytes);
* **`not_implemented_in_libam[]` — 12 functions**, with the MEASURED note that the daemon's own library answers
  `Function "…" is not implemented.` for each. This is a *negative* result that saves work: a shim must **not**
  answer them. That list is the single most valuable complete part of the draft.

**Cost to finish:** `REPORT.md` §11 estimates it — Step 0 (`hwvalue.exe` baseline + values) 0.5 day; identity +
NVRAM geometry answers 1 day; and the ioctl capture C2 is a traced daemon session. **Roughly 2–3 days**, and
shared with the other five keychip titles.

## 5. Proposed test (NOT RUN) + positive control

**One test, and it is deliberately the cheapest one first** — because C2 (the ioctl protocol) is the expensive
half and `REPORT.md` §9.3 records that a registry-only path may make it unnecessary for a start.

**Step 0 — the harness, before any shim exists.**

* Seed `HKLM\SYSTEM\SEGA\SYSTEMPROPERTY\keychip` with `keychipId` / `platformId` / `formatType` in a
  **view-local Wine prefix**, then run `hwvalue.exe` with no shim at all, and record `modeltype`, `region`,
  `dipsw`.

* **Passing result (positive control).** `hwvalue.exe` prints a `modeltype` in `[1-98]`, a `region` that is a
  bit-flag combination of `1|4|8`, and a `dipsw` value — and it does so **both** with the registry seeded and
  without, which would show the values come from the cabinet defaults rather than from us. Those three numbers
  then fill draft fields 1, 2 and 3, and `hwvalue.exe` becomes the regression test for the shim: run it before
  and after, and its output must be unchanged.
* **Failing result (negative control).** With the registry **un**seeded and no shim, `hwvalue.exe` must fail the
  same way the daemon does — i.e. it must not invent values. If it prints plausible numbers with no keychip and
  no registry, then the harness is not exercising the keychip path at all and its output cannot be used to fill
  the descriptor; that is a calibration failure and it must be reported as one. Second control: deliberately
  write a **malformed** `keychipId` (wrong length) and confirm the daemon's own
  `ERROR: invalid KeychipID(%s)` appears — that proves the value is validated and therefore that a correct one
  matters.
* **Only after that**, run C2 (daemon alone, relay trace) to fill `ioctls[]`. Its positive control is written in
  `REPORT.md` §6: `fdd-ioctls.txt` is non-empty and `ioctl-codes.txt` lists distinct codes. Its negative
  control is the documented fallback: if `+relay` shows no `DeviceIoControl`, the daemon reached the device via
  `NtDeviceIoControlFile`, and a trace that reports "no ioctls" from `+relay` alone must **not** be read as "the
  daemon makes no ioctl calls".

## 6. Evidence log

```
# the draft's own open markers (7 TODO fields + 2 empty containers)
grep -n -E 'TODO|\[\]|\{\}' patches/HOTDSD/dongle.sega-keychip.draft.json
  -> 10 modeltype_note "... TODO: confirm which value an ALLS UX cabinet reports."
     12 region_note    "... TODO: 4 (EXP) is INFERRED from config.json language=english; confirm by running
                        hwvalue.exe region after seeding the registry."
     16 formatType_note "TODO: read by amsSystemPropertyGetFormatTypeOnDongle; value not yet measured."
     30 "keychipId": "TODO-20-chars"
     38 mode_note      "amDongleGetBillingMode. TODO: value not yet measured."
     42 "geometry": "TODO: from amyDongleGetGeometryNvram"
     43 "files": {}
     48 "iv": "TODO"
     68 "ioctls": []
  -> 8 occurrences of the literal "TODO", spanning 7 fields, plus files={} and ioctls=[]

# the draft's own scope statement, kept because it is measured and it decides the priority
  line 6 "notes": "... Nothing here is required to reach the game: HOTDSD is measured playable with no keychip
                   at all under TeknoParrot with SEGA's unmodified daemon."

# the layer, with addresses (evidence, not a string list)
sed -n '/A. all dongle\/keychip\/driver strings/,/^$/p' patches/HOTDSD/measurements/05-keychip-probe.txt
  -> 0x0058f8f0 DongleManager Build:Apr 19 2018 ($Rev: 2207 $)
     0x00596820 DongleContext  Build:Apr 19 2018 ($Rev: 1166 $)
     0x005a3a40 amDongle Build:Mar 19 2018 ($Rev: 6676 $)
     0x005a3440 Error amDongleInit!!   0x005a3510 Error amDongleExit!!
     0x005a3a78 Error: amyDongleOpen() failed.    0x005a3b48 Error: amyDongleIsAvailable() %d.
     0x005a3b70 Error: amyDongleGetGeometryNvram() %d.   0x005a3b98 Error: amyDongleGetBillingMode() %d.
     0x005a3f00 Error: amsSystemPropertyGetFormatTypeOnDongle() failed %d.
     0x005a3fd0 Error: amsSystemPropertyGetPlatformIdOnDongle() failed %d.
     0x005a4380 Error: amyDongleReadNvram() failed %d.   0x005a43d0 Error: amyDongleWriteNvram() failed %d.
     0x005a4418 amDongleBillingGetKeychipId  0x005a4830 Error: amyDongleBillingGetCaCertification() failed %d.
     + the 12 "Function \"X\" is not implemented." lines

# are the open values already on disk? searched the whole patch tree
grep -rn -i -E 'ioctl|deviceiocontrol|fdddriver|0x0022|IOCTL_' patches/HOTDSD | head -40
  -> VOLLEDIGHEID.md:282,296,306 and REPORT.md:46,238,239,360,435-436,549-567,692-706,863-864
  -> all of them are METHOD or DESIGN text. No captured ioctl stream, no ioctl-codes.txt, no fdd-ioctls.txt.
grep -rn -i -E 'modeltype|formattype|keychipid|billingMode|geometry' patches/HOTDSD | head -60
  -> the draft itself, the schema fragment, and REPORT.md:892-910 (the worked EXAMPLE descriptor:
     "modeltype": 1, "formatType": 0, "keychipId": "A0000000000000000000",
     "nvram": { "geometry": [ { "index": 0, "size": 4096 } ], ... })
  -> marked in REPORT.md as the target shape, not as a measurement. NOT promoted into the draft here.

# structural soundness of the dump
tail -25 patches/HOTDSD/measurements/completeness.txt
  -> 2081 files, 13095511155 bytes, 0 empty, 32 PE files, truncated PE files: 0
     Hodzero-Win64-Shipping.exe imports=50 system=50 UNRESOLVED=0
     amdaemon.exe imports=23 system=23 UNRESOLVED=0
     Hodzero-Win64-Shipping_org.exe present (51470336) beside Hodzero-Win64-Shipping.exe (54128128)

# the schema fragment is ready and has the widest reach
cat patches/HOTDSD/sega-keychip.schema-fragment.json
  -> adds "sega-keychip" to type.enum, with device / identity / billing / dipsw / nvram / crypto / ioctls /
     not_implemented_in_libam / test_harness, and a 5-step adoption list
     allOf: if type == "sega-keychip" then required ["device","identity"]

# the patched-out function
patches/LOADERS-ZES-GAMES.md -> "Het adres 0x1402b4270 (amDongleIsAvailableInternal) staat in deze build op
                                 cc cc cc cc."
```

**Not measured, and not claimed:** the real `modeltype`, `region`, `formatType`, `keychipId`, `billingMode`,
NVRAM geometry, NVRAM images, crypto IV, and the ioctl codes. All nine require runs; none is blocked on missing
information.
