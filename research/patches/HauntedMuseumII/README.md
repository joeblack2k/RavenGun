# Haunted Museum II

Instellingen: `game.ini`, patches: `patches.ini`.
Bevindingen en meetgeschiedenis: `findings/HauntedMuseum.md` (op de NAS naast de games, niet in deze repo).

## Welke build we hebben (gemeten)

De dump is de **USA 1.00**-build. Dat is niet aangenomen maar gemeten met dezelfde CRC die onze eigen
OpenParrot-kern zelf over de eerste `0x400` bytes van de hoofdmodule rekent: `0x5a7fa241`, en dat is in
`GameDetect.cpp` `GameID::HauntedMuseum2100`. De JP 1.01-build zou `0x42d54499` geven
(`GameID::HauntedMuseum2101J`). Zie `patches/EIGEN-KERN.md` en `patches/kern-herkenning.py`.

**Gevolg voor `patches.ini`:** van de twee versiesets die daar staan, is alleen de set met
`Version = 2100` van toepassing. De drie patches met `Version = 2101J` staan er als documentatie van
de andere build — de kern kiest op grond van die CRC de 2100-tak en past ze nooit toe. Daarom hebben
ze ook geen `Find`: die zou alleen zeggen wat er toevallig op dat adres staat, niet dat het adres goed
is. De patches van de 2100-set hebben er wel een, gemeten op deze exe.

De crash-NOP op `game.exe+0x1391` heeft geen `Find` omdat de byte daar in deze build **al `0x90`** is:
de patch doet op deze dump niets en staat er als documentatie van wat de kern doet.
