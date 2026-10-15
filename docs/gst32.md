# 32-bits GStreamer voor films (`[Launch] Gst32`)

Wine speelt films in Windows-games af via GStreamer: DirectShow/quartz voor `.mpg`, `.m1v` en `.avi`, Media Foundation voor de
rest. Een 32-bits game heeft een 32-bits GStreamer nodig. SteamOS heeft die niet (geen `lib32-gstreamer`); Proton van Steam wel.

```ini
[Launch]
Gst32 = true
```

## Klaarzetten: `scripts/setup-gst32.sh`

Het script kopieert of downloadt niets; het legt alleen links naar wat Steam al heeft. Het maakt
`~/.local/share/ravengun/gst32/` met:

- `proton`: een link naar `files/lib/i386-linux-gnu` van de nieuwste Proton met `libgstlibav`. GE-Proton gaat voor, daarna
  Proton Experimental, daarna `Proton x.y`. Alleen GE-Proton heeft ook `mpegpsdemux` (MPEG-programmastromen).
- `plugins/`: links naar de plug-ins, zonder `protonmediaconverter`. Die verwacht de omgeving van Proton zelf.
- `lib/`: links naar wat die plug-ins verder vragen en alleen in de Steam Linux Runtime (sniper) staat, op SONAME. Nooit de
  glibc van die runtime.
- `BRON.txt`: welke Proton en welke sniper.

`install-user.sh` draait het script één keer. Na een Proton-update draai je het opnieuw.

## Wat rg-direct dan doet

- `LD_LIBRARY_PATH` krijgt vooraan `gst32/proton:gst32/lib`.
- `GST_PLUGIN_SYSTEM_PATH_1_0` wordt `gst32/plugins`.
- `GST_REGISTRY_1_0` wordt een eigen registerbestand.

Staat de map er niet, dan start de game niet. De melding noemt het script.

**In gamescope.** `/usr/bin/gamescope` heeft `cap_sys_nice` (setcap). De dynamische linker draait hem daardoor in
secure-execution mode en haalt `LD_LIBRARY_PATH`, `LD_PRELOAD` en `LD_AUDIT` uit zijn omgeving. Die van de game
verdwijnen dan ook. Daarom zet rg-direct ze achter `--` terug met `/usr/bin/env LD_LIBRARY_PATH=... wine ...`.
Gemeten met Operation G.H.O.S.T. (2026-10-15). Zonder deze stap meldde Wine
`failed to load .../i386-unix/winegstreamer.so: libgstaudio-1.0.so.0: cannot open shared object file`, en de WM-lezer
gaf `0x8007000e` op de titelfilm.

## Gemeten

**Bug Busters** (2026-10-15). Zonder Gst32 gebeurde dit direct na MODE SELECT:

1. `err:quartz:autoplug Failed to create filter for "GStreamer splitter filter", hr 0x8007000e`
2. een NULL-lezing op `0x4456CB`
3. exit 5

Met de GStreamer van GE-Proton10-34 en `libvpx.so.6` uit sniper speelt de intro, en komt het spel via ZONE SELECT in het spel.
