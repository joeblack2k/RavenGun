/* Hulpheader voor het bouwen van OpenParrot met MinGW-w64 in plaats van MSVC (scripts/build-openparrot.sh). Wordt met
 * -include voor elke C++-bron van de kern gezet; wijzigt geen bronbestand. Alleen compiler-eigenaardigheden: de standaard-
 * en wiskundeheaders die MSVC's <windows.h>/<string> stilzwijgend meebrengen, nullptr_t in de globale naamruimte en de
 * SAL-annotaties (__in, __out, ...) uit de Windows SDK. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <math.h>
#include <stddef.h>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <array>
#include <chrono>
#include <codecvt>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>
using std::nullptr_t;
/* Pas na de standaardheaders: libstdc++ gebruikt __out als gewone naam (bits/c++locale.h). */
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __reserved
#define __reserved
#endif
#ifndef __out_data_source
#define __out_data_source(x)
#endif
