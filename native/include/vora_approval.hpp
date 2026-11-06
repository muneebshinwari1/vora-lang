#pragma once
#include "vora_parser.hpp"
#include <chrono>
#include <random>

namespace vora {
inline std::string approval_time() {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
inline std::string approval_id() {
    std::random_device random;
    const char* hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) id += hex[random() & 15];
    return id;
}
inline bool approval_text(const std::string& text, size_t limit, bool required = true) {
    return text.size() <= limit && (!required || !parser_detail::trim(text).empty()) &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
// These files are trusted local state, not signed authorization or authenticated identity.
inline void validate_approvals(const std::map<std::string, std::string>& gates,
                               const std::map<std::string, std::string>& outputs,
                               const nlohmann::json& approvals) {
    if (!approvals.is_object() || approvals.size() > gates.size()) throw std::runtime_error("Invalid approval state.");
    std::set<std::string> ids;
    for (const auto& entry : approvals.items()) {
        const auto& record = entry.value();
        if (!gates.count(entry.key()) || !record.is_object() || record.size() != 8)
            throw std::runtime_error("Unknown or malformed approval record.");
        for (const auto& field : {"status", "request_id", "source", "value", "requested_at", "decided_at", "by", "reason"})
            if (!record.contains(field) || !record[field].is_string()) throw std::runtime_error("Invalid approval fields.");
        const auto status = record["status"].get<std::string>();
        const auto id = record["request_id"].get<std::string>();
        const auto source = record["source"].get<std::string>();
        const auto value = record["value"].get<std::string>();
        const auto digits = [](const std::string& text) { return !text.empty() && text.size() <= 20 && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }); };
        if ((status != "pending" && status != "approved" && status != "rejected") || id.size() != 32 ||
            id.find_first_not_of("0123456789abcdef") != std::string::npos || !ids.insert(id).second ||
            source != gates.at(entry.key()) || !outputs.count(source) || value != outputs.at(source) || value.size() > 4 * 1024 * 1024 ||
            !digits(record["requested_at"].get<std::string>())) throw std::runtime_error("Approval request does not match saved source output.");
        if (status == "pending") {
            if (record["by"] != "" || record["reason"] != "" || record["decided_at"] != "") throw std::runtime_error("Pending approval contains decision metadata.");
        } else if (!approval_text(record["by"].get<std::string>(), 128) || !approval_text(record["reason"].get<std::string>(), 2048) ||
                   !digits(record["decided_at"].get<std::string>())) throw std::runtime_error("Invalid approval decision metadata.");
        if (outputs.count(entry.key()) && (status != "approved" || outputs.at(entry.key()) != value))
            throw std::runtime_error("Approval output has no matching approved decision.");
    }
    for (const auto& gate : gates)
        if (outputs.count(gate.first) && !approvals.contains(gate.first)) throw std::runtime_error("Missing approval for completed gate.");
}
inline void decide_approval(nlohmann::json& approvals, const std::string& step, const std::string& request,
                            const std::string& decision, const std::string& by, const std::string& reason) {
    if ((decision != "approve" && decision != "reject") || !approval_text(by, 128) || !approval_text(reason, 2048))
        throw std::runtime_error("Decision requires approve/reject, --by (1..128 bytes), and --reason (1..2048 bytes), without control characters.");
    if (!approvals.contains(step) || approvals.at(step).at("request_id") != request)
        throw std::runtime_error("Unknown or stale approval request ID.");
    auto& record = approvals.at(step);
    if (record.at("status") != "pending") throw std::runtime_error("Approval already decided; decisions cannot be overwritten.");
    record["status"] = decision == "approve" ? "approved" : "rejected";
    record["by"] = by; record["reason"] = reason; record["decided_at"] = approval_time();
}
}
