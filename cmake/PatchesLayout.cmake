# SPDX-License-Identifier: GPL-3.0-or-later
#
# De productindeling van patches/ na de installatie: <prefix>/patches als echte map naast bin/, lib/ en share/, met de relatieve
# compatibiliteitskoppeling <prefix>/share/ravengun/patches -> ../../patches. Een systeeminstallatie (/ of /usr...) houdt het bij de
# datamap: geen losse /usr/patches. Wordt door install(CODE) in CMakeLists.txt ingelezen na de bestanden van patches/, en staat los
# zodat tests/install_prefix_test.sh hem in scriptmodus kan draaien (cmake -P). Variabelen:
#   RG_PREFIX       CMAKE_INSTALL_PREFIX (voor de keuze systeem of eigen prefix)
#   RG_ROOT         $DESTDIR + CMAKE_INSTALL_PREFIX (waar echt geschreven wordt)
#   RG_DATADIR      share/ravengun
#   RG_TEST_STOP    alleen voor de test: "na-merk" of "na-rename" stopt daar, alsof de installatie onderbroken werd
#
# Het merkbestand <datadir>/.patches-installatie zegt "<prefix>/patches is van de installatie". Het wordt geschreven VOOR de
# rename: een onderbroken eerste omzetting (rename gedaan, koppeling nog niet) laat dan een niet-lege <prefix>/patches MET merk
# achter, en die accepteren cmake/CheckInstallPrefix.cmake en de volgende installatie (anders werd elke latere installatie
# geweigerd). Een bestaande <prefix>/patches gaat alleen weg als hij leeg is of het merk er staat.
set(_rg_dm "${RG_ROOT}/${RG_DATADIR}/patches")
set(_rg_top "${RG_ROOT}/patches")
set(_rg_merk "${RG_ROOT}/${RG_DATADIR}/.patches-installatie")
set(_rg_merktekst "patches/ naast bin/ is door de installatie gemaakt\n")
if(RG_PREFIX STREQUAL "/" OR RG_PREFIX MATCHES "^/usr(/.*)?$")
  # Systeeminstallatie: de datamap is de enige plek (geen /usr/patches).
elseif(IS_SYMLINK "${_rg_dm}")
  # De productindeling staat er al; de bestanden zijn net in <prefix>/patches gezet.
  file(WRITE "${_rg_merk}" "${_rg_merktekst}")
else()
  if(IS_SYMLINK "${_rg_top}")
    message(FATAL_ERROR "${_rg_top} is een koppeling; niet vervangen")
  elseif(EXISTS "${_rg_top}")
    # file(GLOB) geeft "." en ".." niet terug (tests/install_prefix_test.sh, "lege <prefix>/patches"); ".*" vangt verborgen bestanden.
    file(GLOB _rg_inhoud LIST_DIRECTORIES true "${_rg_top}/*" "${_rg_top}/.*")
    if(_rg_inhoud AND NOT EXISTS "${_rg_merk}")
      message(FATAL_ERROR "${_rg_top} bestaat al en is niet van een eerdere installatie; niet vervangen")
    endif()
    file(REMOVE_RECURSE "${_rg_top}")
  endif()
  file(WRITE "${_rg_merk}" "${_rg_merktekst}")
  if(RG_TEST_STOP STREQUAL "na-merk")
    return()
  endif()
  file(RENAME "${_rg_dm}" "${_rg_top}")
  if(RG_TEST_STOP STREQUAL "na-rename")
    return()
  endif()
  file(CREATE_LINK "../../patches" "${_rg_dm}" SYMBOLIC)
endif()
