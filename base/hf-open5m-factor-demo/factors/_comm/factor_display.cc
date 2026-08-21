#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "factors/_comm/factor_entry_registry.h"

namespace factors {
namespace comm {

std::vector<std::string> GetRegisteredFactorsInfo() {
    auto& registry = FactorEntryRegistry::GetInstance();
    auto names = registry.GetAllNames();
    std::sort(names.begin(), names.end());

    std::vector<std::string> lines;
    lines.push_back("--------- Registered Factors Summary ---------");
    lines.push_back("Total registered factors: " + std::to_string(names.size()));

    if (unlikely(names.empty())) {
        lines.push_back("Warning: No factors registered!");
    } else {
        lines.push_back("Factor list:");
        for (size_t i = 0; i < names.size(); ++i) {
            lines.push_back("  [" + std::to_string(i + 1) + "] " + names[i]);
        }
    }
    lines.push_back("----------------------------------------------");
    return lines;
}

void DisplayRegisteredFactors() {
    for (const auto& line : GetRegisteredFactorsInfo()) {
        std::cout << line << std::endl;
    }
}

}  // namespace comm
}  // namespace factors