# SPDX-License-Identifier: GPL-3.0-or-later
#
# Weigert een installatieprefix waarvan <prefix>/share/ravengun de gegevensmap van deze gebruiker is
# (~/.local/share/ravengun, of $XDG_DATA_HOME/ravengun): dat is de gebruikerslaag (patches/, views,
# prefixen, schrijflagen) en niet de applicatie, en het installatiescript zou er patches/ uit verplaatsen.
#
# Wordt door install(CODE) in CMakeLists.txt als eerste installatieregel ingelezen (er is dan nog niets geschreven) en
# staat los zodat tests/install_prefix_test.sh hem in scriptmodus kan draaien (cmake -P). Variabelen:
#   RG_PREFIX     CMAKE_INSTALL_PREFIX
#   RG_DESTDIR    $DESTDIR (mag leeg zijn)
#   RG_DATA_HOME  $XDG_DATA_HOME (mag leeg zijn)
#   RG_HOME       $HOME (mag leeg zijn)
#
# Beide paden worden eerst genormaliseerd (rg_real_path hieronder: dubbele en afsluitende "/", "." en "..", en een
# koppeling in het pad, bijvoorbeeld een HOME die een symlink is). Een vergelijking van de ruwe tekst liet "~/.local/"
# of "~//.local" door, en dan verplaatste de installatie de gebruikerslaag.
# Het pad normaliseren en de symlinks oplossen, ook als het einde (nog) niet bestaat: get_filename_component(REALPATH) laat de koppelingen
# in een niet-bestaand pad ongemoeid, dus lossen we het langste bestaande deel op en plakken we de rest er weer achter.
function(rg_real_path in out)
  get_filename_component(_p "${in}" ABSOLUTE)
  set(_rest "")
  while(NOT EXISTS "${_p}")
    get_filename_component(_name "${_p}" NAME)
    get_filename_component(_dir "${_p}" DIRECTORY)
    if(_dir STREQUAL _p OR _dir STREQUAL "")
      break()
    endif()
    set(_rest "/${_name}${_rest}")
    set(_p "${_dir}")
  endwhile()
  get_filename_component(_p "${_p}" REALPATH)
  set(${out} "${_p}${_rest}" PARENT_SCOPE)
endfunction()

set(_rg_data "${RG_DATA_HOME}")
if(NOT _rg_data AND RG_HOME)
  set(_rg_data "${RG_HOME}/.local/share")
endif()
if(_rg_data)
  rg_real_path("${RG_DESTDIR}${RG_PREFIX}/share/ravengun" _rg_inst)
  rg_real_path("${_rg_data}/ravengun" _rg_user)
  if(_rg_inst STREQUAL _rg_user)
    message(FATAL_ERROR "prefix ${RG_PREFIX} is de gegevensmap van deze gebruiker (${_rg_user}). Installeer daar niet: dat overschrijft de gebruikerslaag. Kies een eigen prefix, bijvoorbeeld ~/.local/opt/ravengun.")
  endif()
endif()
