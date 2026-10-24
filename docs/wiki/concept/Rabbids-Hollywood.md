# Rabbids Hollywood

*Windows, Unity engine. Route: Wine + a plug-in in the game + TCP input. Status: see section 9. **This page is waiting for the game specialist's digest**; what is here comes from the loader side of the project.*

Tags are those from the [Home](Home) page. The page follows the layout of all game pages.

## 1. Platform and cabinet

A Unity game for Windows, for up to four players with lightguns. Details of the cabinet: *pending the specialist's digest.*

## 2. What the game needs

- **A plug-in framework and our plug-in in the game's folder** (**READ**, the project's set-up script). A Unity game loads managed code; the plug-in is how gun input gets into the game without an emulated I/O board.
- **Its own `Plugins` folder as a real local folder.** In a view (see [Loader architecture](Loader-Architecture)) this folder must be copied, not linked: our plug-in files go next to the game's own, and a link would put them on the read-only share. **MEASURED** (2026-10-12): the set-up script used to create a link here; a test now fails if any file of ours would resolve to the share.

*More, including the game's save data: pending the specialist's digest.*

## 3. The official start script

*Pending the specialist's digest.* No comparison from the launcher's start-script reader is available for this title, because it is not started through that launcher.

## 4. The route we use

This is the **Unity plug-in route**, different from the loader-and-core route of the other Windows titles:

```
engine (ravengund) --TCP 127.0.0.1--> plug-in inside the game --> the game's own input classes
```

A set-up script builds the view, places the plug-in framework and the plug-in, and writes the plug-in's configuration. The game is then started under Wine.

## 5. Input

The engine's `unity-tcp` dialect sends the gun state (aim, trigger, the other buttons, per player) over a local TCP connection to the plug-in. **READ** from the engine's source. Coin and start can also be given from the keyboard.

*What was measured in the game: pending the specialist's digest.*

## 6. Timing

*Pending the specialist's digest.*

## 7. What we saw on screen

*Pending the specialist's digest.*

## 8. Linux notes

- The game runs under Wine; the plug-in connects to the engine over the loopback interface, so nothing needs to be shared between the Unix and the Windows side except a TCP port.
- The view must contain real local folders wherever a file of ours is placed (section 2).

*More: pending the specialist's digest.*

## 9. Status

Reported by the game specialist (2026-10-12/13) and not yet written up here in detail: a ten-minute run was completed — 608 s of game time, the game was stopped by us, coin and start were given, and a real level was played. **MEASURED** by the specialist; the numbers and the method will be on this page once the digest is in.

## 10. Open questions

*Pending the specialist's digest.*

---

*Sources so far: the loader side of the project. Nothing on this page concerns the hardware security or licensing layers of the cabinet.*
