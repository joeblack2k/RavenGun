#pragma once
#include <string>

#include "shared.hpp"

namespace ravengund {

// Luistert op een Unix-socket (bestand 0600 in een map met 0700). Draait tot shared.running
// false wordt of wake_fd leesbaar wordt. Trage clients blokkeren de verwerking nooit:
// watch-frames worden overgeslagen als de uitvoerbuffer van een client nog vol zit.
int ipc_listen(const std::string& path);  // -1 bij fout (errno-melding gelogd)
void ipc_thread_main(SharedState& shared, int listen_fd, int wake_fd);

}  // namespace ravengund
