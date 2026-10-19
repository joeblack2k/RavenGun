# SPDX-License-Identifier: GPL-3.0-or-later
#
# Weigert een installatieprefix die de gebruikerslaag of een werkmap kan beschadigen. Het installatiescript verplaatst
# <prefix>/share/ravengun/patches naar <prefix>/patches en gooit daarvoor een bestaande <prefix>/patches weg; een verkeerde prefix
# verplaatst of wist dan de gebruikerslaag (patches/, views, prefixen, schrijflagen) of de patches/ van een repository.
#
# Geweigerd wordt:
#   1. een prefix waarvan <prefix>, <prefix>/patches of <prefix>/share/ravengun gelijk is aan, of ligt in, de gegevensmap van deze
#      gebruiker. Dat is ALTIJD ~/.local/share/ravengun (de daemon draait als systemd-gebruikersdienst en ziet de XDG_DATA_HOME van
#      de installatieshell niet), en daarnaast $XDG_DATA_HOME/ravengun als die absoluut is (een relatieve negeert de daemon ook,
#      zie xdg_data_base in src/ravengun/paths.cpp);
#   2. een prefix die de wortel van een git-werkmap is (daar staat de patches/ van de repository);
#   3. een prefix met een <prefix>/patches die niet aantoonbaar van een eerdere installatie is: niet leeg, geen koppeling
#      <prefix>/share/ravengun/patches -> ../../patches ernaast en geen merkbestand <prefix>/share/ravengun/.patches-installatie
#      (dat schrijft het installatiescript). Ook een <prefix>/patches die zelf een koppeling is, of een
#      <prefix>/share/ravengun/patches die naar iets anders wijst dan ../../patches, wordt geweigerd: daar zou de installatie
#      doorheen schrijven. Een systeemprefix (/ of /usr...) krijgt geen <prefix>/patches en valt hier buiten.
#
# Wordt door install(CODE) in CMakeLists.txt als eerste installatieregel ingelezen (er is dan nog niets geschreven) en
# staat los zodat tests/install_prefix_test.sh hem in scriptmodus kan draaien (cmake -P). Variabelen:
#   RG_PREFIX     CMAKE_INSTALL_PREFIX
#   RG_DESTDIR    $DESTDIR (mag leeg zijn)
#   RG_DATA_HOME  $XDG_DATA_HOME (mag leeg zijn)
#   RG_HOME       $HOME (mag leeg zijn)
#
# Alle paden worden eerst genormaliseerd (rg_real_path hieronder: dubbele en afsluitende "/", "." en "..", en een
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

# <pad> is <wortel> of ligt eronder (beide al genormaliseerd).
function(rg_binnen pad wortel out)
  set(_r FALSE)
  if(pad STREQUAL wortel)
    set(_r TRUE)
  else()
    string(FIND "${pad}" "${wortel}/" _pos)
    if(_pos EQUAL 0)
      set(_r TRUE)
    endif()
  endif()
  set(${out} ${_r} PARENT_SCOPE)
endfunction()

set(_rg_root "${RG_DESTDIR}${RG_PREFIX}")
rg_real_path("${_rg_root}" _rg_p)
rg_real_path("${_rg_root}/patches" _rg_pp)
rg_real_path("${_rg_root}/share/ravengun" _rg_ps)

# 1. De gegevensmap van de gebruiker.
set(_rg_users "")
if(RG_HOME)
  list(APPEND _rg_users "${RG_HOME}/.local/share/ravengun")
endif()
if(RG_DATA_HOME AND IS_ABSOLUTE "${RG_DATA_HOME}")
  list(APPEND _rg_users "${RG_DATA_HOME}/ravengun")
endif()
foreach(_rg_u IN LISTS _rg_users)
  rg_real_path("${_rg_u}" _rg_user)
  foreach(_rg_x IN ITEMS "${_rg_p}" "${_rg_pp}" "${_rg_ps}")
    rg_binnen("${_rg_x}" "${_rg_user}" _rg_in)
    if(_rg_in)
      message(FATAL_ERROR "prefix ${RG_PREFIX} komt uit in de gegevensmap van deze gebruiker (${_rg_user}; ${_rg_x}). Installeer daar niet: dat verplaatst of overschrijft de gebruikerslaag. Kies een eigen prefix, bijvoorbeeld ~/.local/opt/ravengun.")
    endif()
  endforeach()
endforeach()

# 2. De wortel van een git-werkmap (.git is een map, of een bestand bij een worktree).
if(EXISTS "${_rg_p}/.git")
  message(FATAL_ERROR "prefix ${RG_PREFIX} is de wortel van een git-werkmap (${_rg_p}). Installeer daar niet: de installatie zou de patches/ van de repository vervangen. Kies een eigen prefix, bijvoorbeeld ~/.local/opt/ravengun.")
endif()

# 3. Een bestaande <prefix>/patches moet van een eerdere installatie zijn (zie boven).
if(NOT (RG_PREFIX STREQUAL "/" OR RG_PREFIX MATCHES "^/usr(/.*)?$"))
  set(_rg_top "${_rg_root}/patches")
  set(_rg_dm "${_rg_root}/share/ravengun/patches")
  set(_rg_link_ok FALSE)
  if(IS_SYMLINK "${_rg_dm}")
    file(READ_SYMLINK "${_rg_dm}" _rg_doel)
    if(NOT _rg_doel STREQUAL "../../patches")
      message(FATAL_ERROR "${_rg_dm} is een koppeling naar ${_rg_doel} en niet naar ../../patches; de installatie zou daar doorheen schrijven. Haal de koppeling weg of kies een eigen prefix.")
    endif()
    set(_rg_link_ok TRUE)
  endif()
  if(IS_SYMLINK "${_rg_top}")
    message(FATAL_ERROR "${_rg_top} is een koppeling; de installatie zou daar doorheen schrijven of hem weggooien. Haal de koppeling weg of kies een eigen prefix.")
  endif()
  if(EXISTS "${_rg_top}")
    file(GLOB _rg_inhoud LIST_DIRECTORIES true "${_rg_top}/*" "${_rg_top}/.*")
    if(_rg_inhoud AND NOT _rg_link_ok AND NOT EXISTS "${_rg_root}/share/ravengun/.patches-installatie")
      message(FATAL_ERROR "${_rg_top} bestaat al en komt niet aantoonbaar van een eerdere installatie (geen koppeling share/ravengun/patches -> ../../patches en geen merkbestand share/ravengun/.patches-installatie). De installatie zou hem vervangen. Kies een eigen prefix.")
    endif()
  endif()
endif()
