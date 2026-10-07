#pragma once
// Webinterface van de daemon: één eigen thread, niet-blokkerende sockets (poll). Dezelfde
// opdrachtafhandeling als de IPC (commands.hpp). Een trage of vastgelopen browser houdt de
// HID-, camera- of IPC-thread nooit op: de webthread leest alleen een momentopname (korte lock).
#include "ravengun/web/web_core.hpp"
#include "shared.hpp"

namespace ravengund {

// Luistersocket op 127.0.0.1 (of 0.0.0.0 bij cfg.lan). -1 bij een fout (gelogd).
int web_listen(const ravengun::web::WebConfig& cfg);
// Draait tot shared.running false wordt of wake_fd leesbaar wordt.
void web_thread_main(SharedState& shared, ravengun::web::WebConfig cfg, int listen_fd, int wake_fd);

}  // namespace ravengund
