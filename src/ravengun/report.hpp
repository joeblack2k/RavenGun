#pragma once
#include <string>
#include <vector>

#include "ravengun/model.hpp"

namespace ravengun {

struct ReportOptions {
    bool full_addresses = false;  // standaard worden Bluetooth-adressen geredigeerd
};

std::string doctor_human(const std::vector<Check>& checks);
std::string doctor_json(const std::vector<Check>& checks, int exit_code);

std::string devices_human(const Snapshot& snap, const ReportOptions& opt);
std::string devices_json(const Snapshot& snap, const ReportOptions& opt);

}  // namespace ravengun
