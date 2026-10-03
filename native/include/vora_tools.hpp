#pragma once
#include "vora_parser.hpp"
#include <filesystem>
#include <fstream>
#include <memory>

namespace vora {
class ToolRegistry {
    std::filesystem::path root_;
    std::set<std::string> allowed_;
    static constexpr size_t file_limit = 256 * 1024;

    static void schema(const nlohmann::json& args, const std::set<std::string>& required,
                       const std::set<std::string>& optional = {}) {
        if (!args.is_object()) throw std::runtime_error("Tool arguments must be an object.");
        for (const auto& key : required) if (!args.contains(key)) throw std::runtime_error("Missing tool argument: " + key);
        for (const auto& item : args.items())
            if (!required.count(item.key()) && !optional.count(item.key())) throw std::runtime_error("Unknown tool argument: " + item.key());
    }
    static std::string text(const nlohmann::json& args, const std::string& key, size_t limit = file_limit) {
        const auto result = args.at(key).get<std::string>();
        if (result.size() > limit || result.find('\0') != std::string::npos) throw std::runtime_error("Invalid tool text argument.");
        return result;
    }
    std::filesystem::path resolve(const std::string& value) const {
        if (root_.empty()) throw std::runtime_error("Filesystem tools require --workspace.");
        if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos ||
            value.find(':') != std::string::npos || value.find('\\') != std::string::npos)
            throw std::runtime_error("Use a relative workspace path with forward slashes.");
        const std::filesystem::path relative(value);
        if (relative.is_absolute() || relative.has_root_path()) throw std::runtime_error("Absolute tool paths are forbidden.");
        auto current = root_;
        for (const auto& part : relative) {
            if (part == "..") throw std::runtime_error("Parent traversal is forbidden.");
            if (part == "." || part.empty()) continue;
            current /= part;
            const auto status = std::filesystem::symlink_status(current);
            if (std::filesystem::is_symlink(status)) throw std::runtime_error("Tool paths cannot follow symlinks.");
            if (!std::filesystem::exists(status)) throw std::runtime_error("Tool path does not exist.");
        }
        const auto canonical = std::filesystem::canonical(current);
        auto root_it = root_.begin(), path_it = canonical.begin();
        for (; root_it != root_.end(); ++root_it, ++path_it)
            if (path_it == canonical.end() || *root_it != *path_it) throw std::runtime_error("Tool path escaped workspace.");
        return canonical;
    }
    std::string read(const std::string& path) const {
        const auto target = resolve(path);
        if (!std::filesystem::is_regular_file(target)) throw std::runtime_error("Tool path must be a regular file.");
        std::ifstream file(target, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot read tool file.");
        std::string result;
        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
            result.append(buffer, static_cast<size_t>(file.gcount()));
            if (result.size() > file_limit) throw std::runtime_error("Tool file exceeds 256 KiB.");
        }
        if (!file.eof() || result.find('\0') != std::string::npos) throw std::runtime_error("Tool file is not readable text.");
        // JSON serialization validates UTF-8 without executing or parsing file content.
        (void)nlohmann::json(result).dump();
        return result;
    }
public:
    ToolRegistry(std::string workspace, std::set<std::string> allowed) : allowed_(std::move(allowed)) {
        for (const auto& name : allowed_) if (!builtin_tools().count(name)) throw std::runtime_error("Unknown tool grant: " + name);
        if (!workspace.empty()) {
            root_ = std::filesystem::canonical(workspace);
            if (!std::filesystem::is_directory(root_)) throw std::runtime_error("Workspace must be a directory.");
        }
    }
    std::string workspace() const { return root_.generic_string(); }
    const std::set<std::string>& allowed() const { return allowed_; }
    bool permits(const std::string& tool) const { return allowed_.count(tool) != 0; }
    std::string operator()(const std::string& tool, const nlohmann::json& args) const {
        if (!permits(tool)) throw std::runtime_error("Tool was not explicitly allowed: " + tool);
        if (tool == "read_file") {
            schema(args, {"path"});
            return read(text(args, "path", 4096));
        }
        if (tool == "list_files") {
            schema(args, {}, {"path"});
            const auto target = resolve(args.contains("path") ? text(args, "path", 4096) : ".");
            if (!std::filesystem::is_directory(target)) throw std::runtime_error("Tool path must be a directory.");
            nlohmann::json entries = nlohmann::json::array();
            bool truncated = false;
            for (const auto& entry : std::filesystem::directory_iterator(target)) {
                if (entries.size() == 100) { truncated = true; break; }
                const auto status = entry.symlink_status();
                const auto type = std::filesystem::is_symlink(status) ? "symlink" :
                    std::filesystem::is_directory(status) ? "directory" : std::filesystem::is_regular_file(status) ? "file" : "other";
                entries.push_back({{"name", entry.path().filename().generic_string()}, {"type", type}});
            }
            return nlohmann::json({{"entries", entries}, {"truncated", truncated}}).dump();
        }
        if (tool == "search_file") {
            schema(args, {"path", "query"}, {"max_matches"});
            const auto query = text(args, "query", 4096);
            if (query.empty()) throw std::runtime_error("Search query cannot be empty.");
            int maximum = 20;
            if (args.contains("max_matches")) {
                if (!args["max_matches"].is_number_integer()) throw std::runtime_error("max_matches must be an integer.");
                const auto number = args["max_matches"].get<int64_t>();
                if (number < 1 || number > 100) throw std::runtime_error("max_matches must be 1..100.");
                maximum = static_cast<int>(number);
            }
            std::istringstream lines(read(text(args, "path", 4096)));
            nlohmann::json matches = nlohmann::json::array();
            std::string line;
            size_t number = 0;
            bool truncated = false;
            while (std::getline(lines, line)) {
                ++number;
                if (line.find(query) == std::string::npos) continue;
                if (matches.size() == static_cast<size_t>(maximum)) { truncated = true; break; }
                size_t end = std::min(size_t{1024}, line.size());
                while (end < line.size() && end > 0 && (static_cast<unsigned char>(line[end]) & 0xc0) == 0x80) --end;
                matches.push_back({{"line", number}, {"text", line.substr(0, end)}, {"text_truncated", end < line.size()}});
            }
            return nlohmann::json({{"matches", matches}, {"truncated", truncated}}).dump();
        }
        if (tool == "text_stats") {
            schema(args, {"text"});
            const auto value = text(args, "text");
            size_t words = 0, lines = value.empty() ? 0 : 1;
            bool in_word = false;
            for (unsigned char c : value) {
                const bool space = std::isspace(c) != 0;
                if (!space && !in_word) ++words;
                in_word = !space;
                if (c == '\n') ++lines;
            }
            return nlohmann::json({{"bytes", value.size()}, {"words", words}, {"lines", lines}}).dump();
        }
        if (tool == "json_select") {
            schema(args, {"text", "pointer"});
            const auto source = text(args, "text");
            const auto pointer = text(args, "pointer", 4096);
            auto depth_check = [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                if (depth > 64) throw std::runtime_error("JSON data exceeds nesting limit.");
                return true;
            };
            const auto data = nlohmann::json::parse(source, depth_check);
            return data.at(nlohmann::json::json_pointer(pointer)).dump();
        }
        throw std::runtime_error("Unsupported tool.");
    }
};
}
