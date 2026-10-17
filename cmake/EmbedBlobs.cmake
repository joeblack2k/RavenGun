# Bakt bestanden (ook programma's) als bytes in een C++-bronbestand. Gebruik als script, bij het bouwen:
#   cmake -DOUT=<uitvoer.cpp> "-DITEMS=<naam>=<pad>;<naam>=<pad>" -P cmake/EmbedBlobs.cmake
# Het resultaat implementeert syshelper::embedded_blobs() (src/syshelper/embedded.hpp). Gebruikt door ravengun-setup-helper, die
# daardoor als root niets uit een door de gebruiker beschrijfbare map hoeft te lezen (docs/build.md, "Rechten").
if(NOT OUT OR NOT ITEMS)
  message(FATAL_ERROR "EmbedBlobs: OUT en ITEMS zijn verplicht")
endif()
set(decl "")
set(table "")
set(i 0)
foreach(item ${ITEMS})
  if(NOT item MATCHES "^([^=]+)=(.+)$")
    message(FATAL_ERROR "EmbedBlobs: ongeldig item '${item}' (verwacht naam=pad)")
  endif()
  set(name "${CMAKE_MATCH_1}")
  set(src "${CMAKE_MATCH_2}")
  if(NOT EXISTS "${src}")
    message(FATAL_ERROR "EmbedBlobs: ${src} bestaat niet")
  endif()
  file(READ "${src}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(APPEND decl "const unsigned char kBlob${i}[] = {${bytes}0};\n")
  string(APPEND table "  {\"${name}\", kBlob${i}, sizeof(kBlob${i}) - 1},\n")
  math(EXPR i "${i} + 1")
endforeach()
file(WRITE "${OUT}.tmp"
  "// Gegenereerd door cmake/EmbedBlobs.cmake. Niet bewerken.\n#include \"syshelper/embedded.hpp\"\nnamespace ravengun::syshelper {\nnamespace {\n${decl}const EmbeddedBlob kAll[] = {\n${table}  {nullptr, nullptr, 0},\n};\n}  // namespace\nconst EmbeddedBlob* embedded_blobs() { return kAll; }\n}  // namespace ravengun::syshelper\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT}.tmp" "${OUT}")
file(REMOVE "${OUT}.tmp")
