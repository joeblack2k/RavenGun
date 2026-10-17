// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gedeelde bouwstenen van de twee programma's die met rootrechten draaien: ravengun-setup-helper (pkexec, eenmalig bij het
// instellen) en ravengun-bt-helper (sudo, bij het koppelen van een Move). Bewust klein, zonder Qt en zonder shell:
// vaste absolute paden, een lege omgeving voor kindprocessen, atomisch schrijven. Alles wat een gebruiker kan opgeven
// gaat eerst door een van de valid_*-functies hieronder.
#include <sys/types.h>

#include <string>
#include <vector>

namespace ravengun::syshelper {

// Bluetooth-adres als AA:BB:CC:DD:EE:FF (hoofdletters), niet 00:..:00 en niet FF:..:FF.
bool valid_bt_address(const std::string& a);
// Adres in de vorm van een map onder /var/lib/bluetooth (zelfde patroon, ook het adres van de adapter).
bool is_bt_dir_name(const std::string& a);
// Gebruikersnaam zoals useradd ze toestaat: ^[a-z_][a-z0-9_-]{0,31}$ en niet "root".
bool valid_user_name(const std::string& n);

// Schrijft `data` atomisch naar dir/name: tijdelijk bestand in dezelfde map (O_EXCL, O_NOFOLLOW), rechten en eigenaar gezet,
// fsync, rename. uid/gid -1 = niet veranderen. Geeft false met een tekst in err.
bool write_file_atomic(const std::string& dir, const std::string& name, const std::string& data, mode_t mode, uid_t uid, gid_t gid,
                       std::string& err);
// Leest een heel bestand (zonder een symlink op het eind te volgen). Leeg + false als het niet lukt.
bool read_file_nofollow(const std::string& path, std::string& out);
// Als read_file_nofollow, maar vanaf een open bestandsbeschrijving (bv. /proc/self/exe).
bool read_fd_all(int fd, std::string& out);
// mkdir -p met een vaste mode; bestaande mappen blijven zoals ze zijn.
bool make_dirs(const std::string& path, mode_t mode, std::string& err);

// Voert een programma uit (absoluut pad, fork + execve, ALTIJD een lege omgeving), wacht en geeft de exitcode (-1 bij een fout of
// signaal). stdout/stderr van het kind gaan naar `out` als dat niet nul is, anders naar /dev/null. stdin is /dev/null.
int run_program(const std::vector<std::string>& argv, std::string* out, int timeout_ms = 60000);

// Sluit alle bestandsbeschrijvingen boven 2, maakt de omgeving leeg en zet umask 077 (aan het begin van main van een rootprogramma).
void sanitize_process();

}  // namespace ravengun::syshelper
