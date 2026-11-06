#pragma once
#include "vora_state.hpp"
#include <iostream>

namespace vora {
inline int approval_command(int argc, char** argv) {
    if (argc < 3) throw std::runtime_error("Use vora approvals CHECKPOINT or vora decide CHECKPOINT --step NAME --request ID --decision approve|reject --by NAME --reason TEXT.");
    const bool inspect = std::string(argv[1]) == "approvals";
    std::map<std::string, std::string> args;
    for (int i = 3; i < argc; i += 2) {
        const std::string key = argv[i];
        if (inspect || i + 1 >= argc || (key != "--step" && key != "--request" && key != "--decision" && key != "--by" && key != "--reason") ||
            !args.emplace(key, argv[i + 1]).second) throw std::runtime_error("Unknown, duplicate or incomplete approval option.");
    }
    if (!inspect && args.size() != 5) throw std::runtime_error("Decision requires --step, --request, --decision, --by and --reason.");
    StateLock lock(argv[2]);
    auto checkpoint = read_state(argv[2]);
    if (checkpoint.at("format") != "vora-checkpoint" || checkpoint.at("version") != 2 ||
        !checkpoint.at("transaction").is_string() || checkpoint.at("transaction").get<std::string>().empty() ||
        !checkpoint.at("calls").is_number_integer()) throw std::runtime_error("Expected a version-2 approval checkpoint.");
    const auto& identity = checkpoint.at("identity");
    if (!identity.at("max_calls").is_number_integer() || identity.at("max_calls") < 1 || identity.at("max_calls") > 10000 ||
        checkpoint.at("calls") < 0 || checkpoint.at("calls") > identity.at("max_calls")) throw std::runtime_error("Invalid checkpoint call budget.");
    const auto calls = checkpoint.at("calls").get<int>();
    if (calls < 0 || calls > identity.at("max_calls").get<int>()) throw std::runtime_error("Invalid checkpoint call budget.");
    std::map<std::string, std::string> gates, instructions;
    for (const auto& step : identity.at("workflow").at("steps")) {
        if (step.at("kind") != "approval") continue;
        const auto name = step.at("name").get<std::string>(), source = step.at("approval_source").get<std::string>();
        if (!gates.emplace(name, source).second || step.at("dependencies") != nlohmann::json::array({source}))
            throw std::runtime_error("Invalid checkpoint approval declarations.");
        instructions[name] = step.at("prompt").get<std::string>();
    }
    if (gates.empty()) throw std::runtime_error("Checkpoint has no approval gates.");
    auto outputs = checkpoint.at("outputs").get<std::map<std::string, std::string>>();
    auto& approvals = checkpoint.at("approvals");
    validate_approvals(gates, outputs, approvals);
    if (inspect) {
        nlohmann::json requests = nlohmann::json::array();
        for (const auto& entry : approvals.items()) {
            auto record = entry.value(); record["step"] = entry.key(); record["instruction"] = instructions.at(entry.key());
            requests.push_back(std::move(record));
        }
        std::cout << nlohmann::json({{"transaction", checkpoint["transaction"]}, {"calls", calls}, {"requests", requests}}).dump(2) << '\n';
    } else {
        for (const auto& entry : approvals.items())
            if (entry.value()["status"] == "rejected") throw std::runtime_error("This workflow was rejected; start a new job.");
        decide_approval(approvals, args.at("--step"), args.at("--request"), args.at("--decision"), args.at("--by"), args.at("--reason"));
        validate_approvals(gates, outputs, approvals);
        write_state(argv[2], checkpoint);
        // No model, tool, memory or workflow execution occurs in this command.
        std::cout << nlohmann::json({{"step", args.at("--step")}, {"request_id", args.at("--request")},
            {"status", approvals.at(args.at("--step"))["status"]}, {"resume_required", true}}).dump() << '\n';
    }
    return 0;
}
}
