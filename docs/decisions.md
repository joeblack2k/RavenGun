# Beslissingen en afgewezen hypotheses

## 2026-10-07 — Uitvoeringsroute Halo

Vastgesteld: Halo is een native Linux-ELF (platform A = Raw Thrills Linux). De TeknoParrot-UI is native Linux (B), maar de loader voor dit profiel, ElfLdr2/BudgieLoader, is een Windows-component die de Linux-UI via Wine/Proton draait (C). De keten wordt dus: Linux-UI, Wine/Proton, BudgieLoader.exe, Linux-ELF van de game.

Gekozen standaardroute: TeknoParrot (de route uit de projectopdracht), met de officiële ElfLdr2- en Proton-componenten die de UI zelf beheert, in een lokale kopie.

Niet als standaard gekozen: de map bevat `halo.so`, een LD_PRELOAD-dongle-emulator (Sentinel HASP) van onbekende herkomst, met een aangepaste `run_game.sh` (`LD_PRELOAD=halo.so game2`). Dit omzeilt de licentiebeveiliging van de game buiten TeknoParrot om en valt onder "omzeil geen licentie/activatie" uit de opdracht. Daarnaast is het onbekende code met hooking-functionaliteit. Deze route wordt alleen heroverwogen als de gebruiker daar uitdrukkelijk voor kiest.

Gecontroleerd en afgewezen: ElfLdr2 staat niet in de UI-map, en er is geen native Linux-ElfLdr2 gevonden in de strings van de UI-binary.
