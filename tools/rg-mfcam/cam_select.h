// SPDX-License-Identifier: GPL-3.0-or-later
// Two settings of the camera part of rg-mfcam.dll (see TWELFTH TASK in rg_mfcam.cpp), without Windows calls, so that
// tests/test_cam_select.cpp can test them on the host.
//
// RG_MFCAM_SYNTHETIC=WxH  a camera of that size also without the daemon's camera file. The frames are then the gray frame
//                         rg-mfcam already delivers when the file has no frame. For a game that only checks that a camera is
//                         there (Lost Land Adventure's launcher: RSLauncher.exe 0x140041450 asks nbamMfCapture's
//                         mfCapDevList::EnumerateDevices for a count > 0, and raises "08-01 AUTH CAMERA ERROR" otherwise).
//                         The size follows the feed's own limits (camera_feed_format.h): even width, at most 4096x4096.
// RG_MFCAM_MODULE=name    the module whose import table gets the redirection of MF.dll!MFEnumDeviceSources; empty = the exe.
//                         Lost Land's launcher does not import MF.dll itself: nbamMfCapture.dll does.
#pragma once

#include <cstdint>
#include <cstdlib>

namespace rg_camsel {

inline constexpr uint32_t kMaxSide = 4096;

// "640x480" (also "640X480") -> w, h. false (w, h untouched) for anything else: missing parts, trailing text, an odd width,
// 0 or more than kMaxSide.
inline bool parse_size(const char* s, uint32_t& w, uint32_t& h) {
    if (!s) return false;
    uint32_t v[2] = {0, 0};
    for (int part = 0; part < 2; ++part) {
        if (*s < '0' || *s > '9') return false;
        uint32_t n = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10 + static_cast<uint32_t>(*s - '0');
            if (n > kMaxSide) return false;
            ++s;
        }
        v[part] = n;
        if (part == 0) {
            if (*s != 'x' && *s != 'X') return false;
            ++s;
        }
    }
    if (*s != '\0') return false;
    if (v[0] == 0 || v[1] == 0 || (v[0] & 1u) != 0) return false;
    w = v[0];
    h = v[1];
    return true;
}

}  // namespace rg_camsel
