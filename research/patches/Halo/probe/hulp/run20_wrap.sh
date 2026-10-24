#!/bin/bash
# Wrapper zodat systemd-run de uitvoer naar een bestand schrijft.
exec bash /home/deck/projects/ravengun-patches/patches/Halo/probe/run20_uitvoeren.sh > /home/deck/run20.out 2>&1
