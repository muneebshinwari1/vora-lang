#pragma once
#include <fstream>
#include <stdexcept>
#include <string>

namespace vora {
// Read incrementally: checking size after reading the entire file is too late.
inline std::string read_bounded(const std::string& path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read file: " + path);
    std::string result;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
        const auto bytes = static_cast<size_t>(file.gcount());
        if (bytes > limit - result.size()) throw std::runtime_error("File exceeds byte limit: " + path);
        result.append(buffer, bytes);
    }
    if (!file.eof()) throw std::runtime_error("Failed to read file: " + path);
    return result;
}
}
