#!/usr/bin/env bash
# Builds the RavenGun replacement shaiolib.dll (PE32 i386) with the project's
# self-contained mingw-w64 toolchain. The i686 compiler is called directly
# with -B, like the mingw-gcc wrapper does for x86_64.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "missing: $CC" >&2; exit 1; }
cd "$HERE"
# The export list (same 52 names, order and ordinals as Konami's shaiolib.dll). It lives in this script and is written to a
# temporary .def for the linker, because the linker only recognises such a file by its .def extension.
DEF="$(mktemp -d)/shaiolib.def"
cat > "$DEF" <<'DEF_EOF'
LIBRARY shaiolib.dll
EXPORTS
    shaiolib_10Key_disable @1
    shaiolib_10Key_enable @2
    shaiolib_10Key_status @3
    shaiolib_BUTTON_status @4
    shaiolib_CARD_start @5
    shaiolib_CARD_status @6
    shaiolib_CARD_status_clear @7
    shaiolib_CARD_stop @8
    shaiolib_COIN_blocker_close @9
    shaiolib_COIN_blocker_open @10
    shaiolib_COIN_consume @11
    shaiolib_COIN_count @12
    shaiolib_COIN_countup @13
    shaiolib_DEBUG_GUN_realtime_pos @14
    shaiolib_DEBUG_GetComPort1Buffer @15
    shaiolib_DEBUG_GetComPort2Buffer @16
    shaiolib_DEBUG_secure_get_eeprom_network @17
    shaiolib_DEBUG_secure_get_eeprom_software @18
    shaiolib_DEBUG_secure_get_id_network @19
    shaiolib_DEBUG_secure_get_id_software @20
    shaiolib_GUN_disable @21
    shaiolib_GUN_enable @22
    shaiolib_GUN_flash_off @23
    shaiolib_GUN_flash_on @24
    shaiolib_GUN_reaction @25
    shaiolib_GUN_set_reaction_pattern @26
    shaiolib_GUN_status @27
    shaiolib_LED_change @28
    shaiolib_QCMODE_get_error @29
    shaiolib_QCMODE_secure_get_id_network @30
    shaiolib_QCMODE_secure_get_id_software @31
    shaiolib_QCMODE_secure_get_status_network @32
    shaiolib_QCMODE_secure_get_status_software @33
    shaiolib_SECURE_check @34
    shaiolib_SECURE_check_no_error @35
    shaiolib_SECURE_is_renewal @36
    shaiolib_SECURE_renew @37
    shaiolib_SYSINFO_get_hardware_id @38
    shaiolib_SYSINFO_get_mother_board @39
    shaiolib_SYSINFO_get_software_id @40
    shaiolib_SYSINFO_get_system_id @41
    shaiolib_get_lasterror @42
    shaiolib_initialize @43
    shaiolib_is_boot_all @44
    shaiolib_is_boot_board_button @45
    shaiolib_is_boot_board_gun @46
    shaiolib_is_boot_comport @47
    shaiolib_is_error @48
    shaiolib_reset_error @49
    shaiolib_start_boot @50
    shaiolib_terminate @51
    shaiolib_update @52
DEF_EOF
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" \
  -B"$SR/usr/i686-w64-mingw32/" \
  -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" \
  -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe \
  -nostdlib -nostartfiles \
  -o "${RG_SHAIO_OUT:-dropin/shaiolib.dll}" shaiolib_rg.c "$DEF" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  -Wl,--image-base,0x10000000 -Wl,-s \
  -lkernel32 > build.log 2>&1
echo "build ok: $(sha256sum "${RG_SHAIO_OUT:-dropin/shaiolib.dll}")"
