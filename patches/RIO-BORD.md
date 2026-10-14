# RIO-BORD — one open item shared by two titles: the Raw Thrills RIO board

**Written:** 2026-10-14. **Titles:** Jurassic Park Arcade and NERF Arcade. Both run their attract loop on
our own launcher, and both put the same sentence on every frame: **"RIO Disconnected" / "RIO
DISCONNECTED"**. It is one missing piece of hardware, so it is one open item: fixing it once counts
twice.
**Labels:** **MEASURED** = read from the files or from a run; **DERIVED** = concluded; **NOT DETERMINED**.

## What is measured

| | Jurassic Park | NERF Arcade |
|---|---|---|
| binary | `game`, ELF 32-bit, the RIO code linked in | `Nerf.x86_64`, ELF 64-bit; the RIO code is `librio.so` (434 exports: `RIO_Init`, `RIO_Connect`, `RIO_GetReport`, `RIO_SampleInput`, `RIO_SW_Count`, `RIO_ADC_Val`, `RIO_GunModeSet`, `RIO_CoinMeterAdd`, …) |
| the call that opens the board | `pmrt_usb_open(0x0c70, 0x0780)`, twice (the instruction pair `movl $0x780,4(%esp); movl $0xc70,(%esp)` at `0x836b178` and `0x836b245`) | `pmrt_usb_open(0x0c70, 0x0780)` in `RIO_Connect` and in `RIO_ConnectEx` |
| how it reaches USB | libusb linked in; strings `libusb_open`, `/dev/bus/usb` | libusb linked into `librio.so`; strings `libusb_*`, `/dev/bus/usb`, "could not find usbfs" |
| what the game says without it | log: `InputInit(): RIO not connected (error code: -3)`; banner "RIO Disconnected" | banner "RIO DISCONNECTED"; nothing in its log |
| what else it shows | runs its attract loop (12 min, 2026-10-13) | runs its attract loop (12 min 46 s, 2026-10-14) |

**So both games look for the same USB device — vendor `0x0C70`, product `0x0780` — with the same
library code.** The board's bootloader is opened as vendor `0x15A2` (`RIO_BL_Connect`), and the library
names the microcontroller in its exports (`rio_jm60_erase_range`).

NERF also carries `libRIO2.so`, the second-generation board, which is a **serial** device
(`rio2_serial_open`, `/dev/%s`, `tcsetattr`). Halo: Fireteam Raven uses that one; its inputs were done
differently (a shim that writes the game's switch table) and it is not part of this item.

## What the board is to the games

START, coin, TEST/service and, as far as is known, the guns. Without it both titles **demo and cannot be
played** on our launcher. No input of any kind has been sent to either title on this route.

## Where one fix could attach — NOT DETERMINED which

1. **A virtual USB device `0c70:0780`** that libusb finds under `/dev/bus/usb`. Both games would see it
   unmodified, and so would any other RIO title. It needs a virtual host controller in the kernel
   (`vhci_hcd`/usbip, or `dummy_hcd` with a gadget) and root; whether SteamOS ships those modules was
   not checked. The daemon's existing virtual devices are `uhid` devices, which are not USB devices and
   which libusb does not list.
2. **At the library:** for NERF a replacement `librio.so` with the `RIO_*` functions the game calls; for
   Jurassic Park hooks on the same functions inside its executable. One model of the board, two
   attachment points.

Either way the first piece of work is the same and is shared: **the board's report format** — which
bits are the switches and coins, where the gun values are, what the game sends back (lamps, solenoids,
coin meter) and what "connected" means to `RIO_Connected`. It can be read from `librio.so`, which is
not stripped of its exported names; the same code is in Jurassic Park's executable.

## Not determined

* The report format (above), and whether the board answers a challenge (`g_rio_encryption`,
  `RIO_EnableEncryption` are exported).
* Whether the guns of either cabinet go through the RIO or through a separate device.
* Which other titles on the list use the first-generation RIO.
