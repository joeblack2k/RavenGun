/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pigs Might Fly (Sega Amusements Europe): the USB HID I/O board that Sega/Shell/Shell.exe looks for, as pure logic. The DLL
 * (rg_pigsio.c) answers the shell's SetupAPI/HID/ReadFile/WriteFile calls with this board; a native test
 * (tests/pigsio_board_test.c) checks it. No Windows calls, no C library beyond <stdint.h>/<stddef.h>.
 *
 * WHERE THE FORMAT COMES FROM (read statically from Shell.exe, size 546304; nothing taken from any other program):
 *   - InitIOBoard 0x413ab0: SetupDiGetClassDevsA(HidD_GetHidGuid, 0, 0, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE), up to 256
 *     interfaces, CreateFileA(path, GENERIC_READ|GENERIC_WRITE, share 3, OPEN_EXISTING, 0) on each, HidD_GetAttributes:
 *     only VendorID 0x1043 and ProductID 0x0000 are kept (VersionNumber goes to 0x491d3c), the others are closed. One kept
 *     device sets the "board found" flag 0x491d34, which is all the boot step 3 (0x405a70, "Status: IO Board OK") checks.
 *     Each kept device then gets HidD_GetPreparsedData + HidP_GetCaps (must return HIDP_STATUS_SUCCESS 0x110000) and is named
 *     from a table at 0x4843c8 by (UsagePage, Usage): GAMEPAD = (1, 5), JOYSTICK (1, 4), SYSTEM (1, 0x80), TRACKBALL (1, 2),
 *     KEYBOARD (1, 6). Only the GAMEPAD collection is ever read or written.
 *   - Read 0x413740: ReadFile(handle, buf, 15) on the GAMEPAD collection; a failed ReadFile clears the found flag.
 *   - Write 0x413830: WriteFile(handle, {0, data, cmd, ...}, caps.OutputReportByteLength).
 *   - The input report (15 bytes, byte 0 = report id 0):
 *       [1], [2]  gun X and Y (0x40af13, 0x40578e: raw bytes, scaled by the shell's own gun calibration "Aim at top left Cross")
 *       [6], [7]  switches: input code c (1..16) is bit (c-1)%8 of byte 6 + (c-1)/8, set = on (0x416710, table 0x485060).
 *                 The switch test (0x40ce30, labels 0x48339c) names the codes: 1 START BUTTON, 5 SEESAW TRIGGER, 8 TICKET NOTCH,
 *                 9 KEY SWITCH, 15 COIN INPUT; TEST and SERVICE come from Shell/config.ini [system] TEST=13, SERVICE=14
 *                 (0x405900: value - 1 indexes a 1..15 table, so code = the value).
 *       [9], [10] coin counter, u16 little endian (0x405240 "cred": the shell compares it with its stored value)
 *       [11]      firmware version, BCD, shown as "%d.%d" in the test menu (0x40fd70; 0 or > 0x99 = NOT DETECTED)
 *       [12]      reply code of the last command (bit 7 set = a reply is present, 0x413ffb), [13], [14] reply data
 *   - The handshake (thread 0x414440, state machine 0x414030, at most 2 s): IDREQUEST1 sends cmd 0xE0 data 0x74 and waits for
 *     reply 0xB0 (its data byte, run through 0x4139c0 with key 0x74, is kept); IDREQUEST2 sends 0xE1 data 0x09 and waits for 0xB1
 *     (its data byte through 0x4139c0 with key 0x09 is kept); UNLOCK1 sends 0xF0 with 0x74 run through 0x413a20 with 0x69 then
 *     0x46, waits for 0xB2; UNLOCK2 sends 0xF1 with 0x09 through 0x413a20 with 0xE9 then 0x3B, waits for 0xB3 ("ALL OK"). The
 *     shell does not check the reply data itself: the two kept bytes become the first two bytes of the 15-byte block at
 *     [0x48b3dc] (0x414ef0) that the Dinkey key code (DK2WIN32 ordinal 20, 0x414e94) is given. So the shell passes the board
 *     step with any reply data; whether the Dinkey step needs particular values is for the dongle work.
 *   - Other output reports: the lamp word 0x491f6c (0x413fac: [1] = low byte, [2] = high byte; set/cleared by 0x41b4b0/0x41b4d0),
 *     and the test menu's cmd 0x81 data 1 (0x410646), whose reply the shell compares with 0x80 (0x410663).
 */
#ifndef RG_PIGS_BOARD_H
#define RG_PIGS_BOARD_H

#include <stddef.h>
#include <stdint.h>

#define RG_PIGS_VID 0x1043u
#define RG_PIGS_PID 0x0000u
#define RG_PIGS_VERSION 0x0100u   /* HidD_GetAttributes VersionNumber; the shell only stores it */
#define RG_PIGS_FIRMWARE 0x10u    /* report byte 11, BCD: "1.0" in the test menu */
#define RG_PIGS_USAGE_PAGE 0x01u  /* Generic Desktop */
#define RG_PIGS_USAGE 0x05u       /* Game Pad: the shell's "GAMEPAD" collection */
#define RG_PIGS_IN_LEN 15u        /* ReadFile length of 0x413740 */
#define RG_PIGS_OUT_LEN 3u        /* report id, data, command: all the shell fills in */

/* Input report offsets. */
enum {
    RG_PIGS_IN_GUN_X = 1,
    RG_PIGS_IN_GUN_Y = 2,
    RG_PIGS_IN_SW0 = 6,
    RG_PIGS_IN_SW1 = 7,
    RG_PIGS_IN_COIN_LO = 9,
    RG_PIGS_IN_COIN_HI = 10,
    RG_PIGS_IN_FIRMWARE = 11,
    RG_PIGS_IN_REPLY = 12,
    RG_PIGS_IN_REPLY_D0 = 13,
    RG_PIGS_IN_REPLY_D1 = 14
};

/* Input codes (1..16) as the shell names them. */
enum {
    RG_PIGS_CODE_START = 1,
    RG_PIGS_CODE_SEESAW = 5,
    RG_PIGS_CODE_TICKET_NOTCH = 8,
    RG_PIGS_CODE_KEY_SWITCH = 9,
    RG_PIGS_CODE_TEST = 13,
    RG_PIGS_CODE_SERVICE = 14,
    RG_PIGS_CODE_COIN = 15
};

/* Commands the shell sends (output byte 2) and the reply codes it waits for (input byte 12). */
enum {
    RG_PIGS_CMD_ID1 = 0xE0,
    RG_PIGS_CMD_ID2 = 0xE1,
    RG_PIGS_CMD_UNLOCK1 = 0xF0,
    RG_PIGS_CMD_UNLOCK2 = 0xF1,
    RG_PIGS_CMD_TEST81 = 0x81,
    RG_PIGS_REPLY_ID1 = 0xB0,
    RG_PIGS_REPLY_ID2 = 0xB1,
    RG_PIGS_REPLY_UNLOCK1 = 0xB2,
    RG_PIGS_REPLY_UNLOCK2 = 0xB3,
    RG_PIGS_REPLY_TEST81 = 0x80
};

/* Shared memory of the direct route (tp-shm, layout WartranTroopers in packaging/tp-shm-profiles.json). */
#define RG_PIGS_SHM_SIZE 64u
enum {
    RG_PIGS_SHM_CONTROL = 8, /* u32 little endian */
    RG_PIGS_SHM_P1X = 12,
    RG_PIGS_SHM_P1Y = 13
};
enum {
    RG_PIGS_BIT_TEST = 0,
    RG_PIGS_BIT_SERVICE = 1,
    RG_PIGS_BIT_COIN1 = 2,
    RG_PIGS_BIT_P1_START = 6,
    RG_PIGS_BIT_P1_TRIGGER = 7
};

typedef struct {
    uint8_t id_reply[2];   /* data byte of the 0xB0 and 0xB1 replies (synthetic; 0 by default) */
    uint8_t reply[3];      /* reply code + data, kept in every input report until the next command */
    uint8_t last_cmd, last_data;
    uint16_t lamps;        /* last lamp word written */
    uint16_t coins;
    int coin_was_down;
    unsigned commands;     /* number of command reports seen */
    unsigned lamp_writes;
} rg_pigs_board;

void rg_pigs_board_init(rg_pigs_board *b);

/* One output report (WriteFile from the shell; len includes the report id byte). Returns 1 when it was a command the board
 * answers, 0 for a lamp write or anything too short. */
int rg_pigs_board_output(rg_pigs_board *b, const uint8_t *rep, size_t len);

/* Builds the input report (RG_PIGS_IN_LEN bytes) from the 64 bytes of shared memory (NULL: everything released). */
void rg_pigs_board_input(rg_pigs_board *b, const uint8_t *shm, uint8_t *rep);

/* The shell's own byte transforms, ported for the log and the test: 0x4139c0 (applied to a reply data byte with a key) and
 * 0x413a20 (applied to a challenge before the unlock commands). They are each other's inverse. */
uint8_t rg_pigs_shell_reply_xform(uint8_t v, uint8_t key);
uint8_t rg_pigs_shell_unlock_xform(uint8_t v, uint8_t key);

#endif
