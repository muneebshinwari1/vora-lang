#pragma once
#include "vora_runtime.hpp"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vora {
// One writer per state file. An interrupted process can leave a lock directory;
// the operator must verify no writer remains before removing it.
class StateLock {
    std::filesystem::path path_;
public:
    explicit StateLock(const std::filesystem::path& target) : path_(target.string() + ".lock") {
        if (!std::filesystem::create_directory(path_))
            throw std::runtime_error("State file is locked: " + target.string());
    }
    ~StateLock() { std::error_code error; std::filesystem::remove(path_, error); }
    StateLock(const StateLock&) = delete;
    StateLock& operator=(const StateLock&) = delete;
};

inline nlohmann::json read_state(const std::filesystem::path& path) {
    const auto raw = read_bounded(path.string(), 8 * 1024 * 1024);
    auto depth_limit = [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
        if (depth > 64) throw std::runtime_error("State exceeds nesting limit.");
        return true;
    };
    return nlohmann::json::parse(raw, depth_limit);
}

inline void write_state(const std::filesystem::path& path, const nlohmann::json& data) {
    const auto raw = data.dump(2);
    if (raw.size() >= 8 * 1024 * 1024) throw std::runtime_error("State exceeds 8 MiB including its newline.");
    const std::filesystem::path temporary(path.string() + ".tmp");
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) throw std::runtime_error("Cannot create state temporary file.");
        file << raw << '\n';
        file.flush();
        if (!file) throw std::runtime_error("Cannot flush state file.");
        file.close();
        if (!file) throw std::runtime_error("Cannot close state file.");
#ifdef _WIN32
        const HANDLE flush_handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (flush_handle == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open state for durable flush.");
        const bool flushed = FlushFileBuffers(flush_handle) != 0;
        CloseHandle(flush_handle);
        if (!flushed) throw std::runtime_error("Cannot durably flush state file.");
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace state file.");
#else
        const int fd = ::open(temporary.c_str(), O_WRONLY);
        if (fd < 0) throw std::runtime_error("Cannot open state for durable flush.");
        const int flushed = ::fsync(fd);
        const int closed = ::close(fd);
        if (flushed != 0 || closed != 0) throw std::runtime_error("Cannot durably flush state file.");
        const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
        const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0) throw std::runtime_error("Cannot open state directory for durable flush.");
        // Probe support before replacing the old file. Some network filesystems
        // do not implement directory fsync and are unsupported for this contract.
        if (::fsync(directory) != 0) {
            ::close(directory);
            throw std::runtime_error("State directory does not support durable flush.");
        }
        std::error_code rename_error;
        std::filesystem::rename(temporary, path, rename_error);
        if (rename_error) {
            ::close(directory);
            throw std::runtime_error("Cannot replace state file.");
        }
        const int synced = ::fsync(directory);
        const int directory_closed = ::close(directory);
        if (synced != 0 || directory_closed != 0)
            throw std::runtime_error("State replaced, but directory durability is uncertain.");
#endif
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}

inline nlohmann::json workflow_identity(const Workflow& workflow) {
    nlohmann::json result = {{"name", workflow.name}, {"input", workflow.input_name},
        {"output", workflow.output}, {"agents", nlohmann::json::object()},
        {"steps", nlohmann::json::array()}, {"validations", nlohmann::json::array()},
        {"repairs", nlohmann::json::array()}, {"tools", workflow.tools}};
    for (const auto& agent : workflow.agents) result["agents"][agent.first] = agent.second.role;
    for (const auto& step : workflow.steps)
        result["steps"].push_back({{"name", step.name}, {"agent", step.agent},
            {"prompt", step.prompt}, {"dependencies", step.dependencies}, {"kind", step.kind}});
    for (const auto& rule : workflow.validations)
        result["validations"].push_back({{"step", rule.step}, {"kind", rule.kind},
            {"value", rule.value}, {"number", rule.number}});
    for (const auto& repair : workflow.repairs)
        result["repairs"].push_back({{"step", repair.step}, {"max", repair.max_attempts}});
    return result;
}

inline nlohmann::json empty_memory() {
    return {{"format", "vora-memory"}, {"version", 1}, {"agents", nlohmann::json::object()}};
}

inline std::map<std::string, std::string> memory_context(const nlohmann::json& memory, const Workflow& workflow) {
    if (memory.at("format") != "vora-memory" || memory.at("version") != 1 || !memory.at("agents").is_object())
        throw std::runtime_error("Unsupported memory format.");
    std::map<std::string, std::string> result;
    for (const auto& agent : workflow.agents) {
        if (!memory["agents"].contains(agent.first)) continue;
        const auto& item = memory["agents"].at(agent.first);
        if (item.at("role") != agent.second.role) continue; // A role change invalidates prior context.
        const auto& entries = item.at("entries");
        if (!entries.is_array() || entries.size() > 4) throw std::runtime_error("Invalid memory history.");
        for (const auto& entry : entries) {
            const auto task = entry.at("input").get<std::string>();
            const auto output = entry.at("output").get<std::string>();
            if (task.size() > 2048 || output.size() > 4096) throw std::runtime_error("Memory entry exceeds limits.");
            result[agent.first] += "Prior task: " + task + "\nPrior output: " + output + "\n";
        }
    }
    return result;
}

inline std::string bounded_text(const std::string& text, size_t limit) {
    size_t end = std::min(limit, text.size());
    while (end < text.size() && end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    return text.substr(0, end);
}

inline void remember(nlohmann::json& memory, const Workflow& workflow, const std::string& input,
                     const std::map<std::string, std::string>& outputs) {
    for (const auto& step : workflow.steps) {
        if (step.kind == "tool") continue;
        auto& item = memory["agents"][step.agent];
        const auto& role = workflow.agents.at(step.agent).role;
        if (!item.is_object() || item.value("role", "") != role)
            item = {{"role", role}, {"entries", nlohmann::json::array()}};
        auto& entries = item["entries"];
        entries.push_back({{"input", bounded_text(input, 2048)}, {"output", bounded_text(outputs.at(step.name), 4096)}});
        while (entries.size() > 4) entries.erase(entries.begin());
    }
}
}
