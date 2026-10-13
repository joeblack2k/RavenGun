#!/bin/bash
# Which preloads are REALLY mapped in the Jurassic Park game process (run from a script file so pgrep does not match itself)
echo "now $(date +%H:%M:%S)"
for p in $(pgrep -f '^/rg/bin/rg-elfloader /pm/game'); do
  cls=$(head -c 5 /proc/$p/exe | tail -c 1 | od -An -tu1 | tr -d ' ')
  echo "pid $p comm $(cat /proc/$p/comm) class $([ "$cls" = 1 ] && echo ELF32 || echo ELF64) threads $(ls /proc/$p/task | wc -l)"
  for lib in libravengun-hasphook ravengun-hasp.so librg-threadprio libSDL-1.2 libSDL2-2.0 libGL.so libgallium; do echo "    maps: $lib -> $(grep -c "$lib" /proc/$p/maps) mappings"; done
  echo "    env: $(tr '\0' '\n' < /proc/$p/environ | grep -E '^(LD_PRELOAD|RG_ELF_SLOT_TABLE|SDL12COMPAT_OPENGL_SCALING|RAVENGUN_HASP_GAME)=' | tr '\n' ' ')"
done
