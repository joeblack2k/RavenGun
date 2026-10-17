// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <string>

#include "syshelper/embedded.hpp"
#include "syshelper/setup_content.hpp"

namespace ravengun::syshelper {

SetupContent embedded_content() {
    SetupContent c;
    for (const EmbeddedBlob* b = embedded_blobs(); b->name; ++b) {
        const std::string data(reinterpret_cast<const char*>(b->data), b->size);
        const std::string name = b->name;
        if (name == kBtHelperName) c.bt_helper = data;
        else c.rules.push_back({name, data});
    }
    return c;
}

}  // namespace ravengun::syshelper
