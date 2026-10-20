# Mario Kart Arcade GP DX: racing with a controller + webcam (plan)

Status 2026-10-11. The owner's goal: be able to race MKDX with an ordinary controller (Xbox or Steam
Controller), and use the PS Eye camera for the game's player photo. Not a lightgun game.

Building has not started yet (another session is working in `patches/` and the patch layers at the same
time; to avoid git conflicts the code waits until that one is done). This is the plan and the open points.

## What we know

- Two versions on the NAS: `MKDX 1.10.22` and `MKDX 1.18.16`. Namco ES3, 32-bit, D3D11, 1360x768.
- TeknoParrot profiles: `MKDX`, `MKDX118`, `MKDXUSA`, `MKDXUSA106` — EmulationProfile `NamcoMkdx(Usa)`,
  `EmulatorType = TeknoParrot`, **no Patreon** (so no subscription needed).
- 1.18.16 has `bngrw.dll` (Namco ES3X layer, like Time Crisis 5); that is not yet in our own
  OpenParrot build, so for now MKDX keeps the loader/core of the TeknoParrot runtime.
- Buttons in the profile: Test, Service, Coin, Start, **Wheel Axis** (+ Left/Right), **Gas**, **Brake**,
  **Item**, Menu Up/Down, Enter Switch, **Mario Button**, Banapass Card Entry.
- Settings: `Input API = DirectInput | XInput`, `CustomResolution`, `Experimental Webcam Hack`,
  and ALL.Net network fields (server/gateway/player id) — offline the core handles that itself.
- Camera: NamCam, `Data/Camera/*.canm`; the profile has `Experimental Webcam Hack`.

## Part 1 — Controller for racing

Two possible routes.

**Route A (preferred, try first): the game reads the controller itself.**
An Xbox pad in the Wine prefix shows up via XInput/DirectInput; the TeknoParrot core can bind axes and
buttons to its profile inputs (Wheel = stick X, Gas/Brake = triggers, Item/Mario/Start/Coin =
buttons). This is also how it works on a PC cabinet.
- Blocker in our direct start: `import-tp-profiles.py` deliberately does **not** import the TeknoParrot
  input mappings (JoystickButtons) — our input lives in `[Input]`/`[Bindings]`. For MKDX we have to
  leave those bindings in place (or add our own `[Input] Route = tp-native` that writes the profile
  bindings into `teknoparrot.ini` and leaves the controller in the prefix).
- Steam Controller: only works as XInput/DirectInput via Steam Input or a driver; an Xbox pad is
  the easy first step (raw XInput). Not launching via Steam = no Steam Input layer.
- Open: exactly how the core reads the controller (question 82 to Johan); dead zone / center calibration.

**Route B: RavenGun reads the controller and writes the shared memory (like the lightgun routes).**
Consistent with our approach, but it requires the **NamcoMkdx shared-memory layout** (offset/width of
wheel/gas/brake, button bits) — that is question 81 to Johan, not yet answered. Only build once we have it.

Recommendation: start with route A (no layout needed), fall back to B if A does not work.

## Part 2 — Webcam (player photo)

- The PS Eye shows up as `/dev/video*` on the system. MKDX is not a lightgun game, so the tracker/camera
  is free to use as a webcam (do check that RavenGun does not claim the camera during MKDX).
- The game captures a player photo via NamCam. How (DirectShow, device index, resolution) and what
  `Experimental Webcam Hack` does: question 83 to Johan.
- Wine side: Wine's video capture (dshow → v4l2) must see the camera. To be tested: which Wine/Proton
  version supports that. The photo is presumably optional — first check whether the game races fine
  without a camera; then the webcam is an extra, not a blocker.

## Order once we build (requires the TV + a controller plugged in)

1. Add MKDX to RavenGun (`import-tp-profiles.py`, own `patches/MKDX*/`), get it to start up to the
   attract/menu. 1.18.16 with the TeknoParrot loader (bngrw).
2. Route A: keep/activate the profile input, an Xbox pad in the prefix, bind to Wheel/Gas/Brake and
   the buttons. Drive on the TV and measure the axes.
3. If A fails: wait for Johan's layout (question 81) and build route B.
4. Webcam: check whether the game races without a camera; then offer the PS Eye through Wine's capture
   and test the photo.

## Open points / dependencies

- Questions 81, 82, 83 to Johan (`findings/QA/vragen.txt`).
- The TV and a physical controller (and for the photo the PS Eye) are needed to verify.
- A second builder is not running in parallel right now, to avoid conflicts with the ongoing patch-layer session.
