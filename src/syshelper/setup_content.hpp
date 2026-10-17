// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "syshelper/setup_core.hpp"

namespace ravengun::syshelper {

// De ingebakken inhoud (udev-regels en ravengun-bt-helper); self_exe blijft leeg (main vult dat in).
SetupContent embedded_content();

}  // namespace ravengun::syshelper
