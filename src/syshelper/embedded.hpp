// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Wat ravengun-setup-helper installeert, tijdens het bouwen ingebakken (cmake/EmbedBlobs.cmake): de udev-regels uit
// packaging/udev en het programma ravengun-bt-helper. Gegenereerd bestand: embedded.cpp in de builddirectory.
#include <cstddef>

namespace ravengun::syshelper {

struct EmbeddedBlob {
    const char* name;
    const unsigned char* data;
    size_t size;
};

// Alle ingebakken bestanden; het laatste element heeft name == nullptr.
const EmbeddedBlob* embedded_blobs();

}  // namespace ravengun::syshelper
