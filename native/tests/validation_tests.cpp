#include "vora_runtime.hpp"
#include <iostream>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const std::string keep = R"({"verdict":"keep","issues":[],"instruction":"Keep the draft unchanged."})";
        const std::string revise = R"({"verdict":"revise","issues":["Unsupported promise"],"instruction":"Remove the promise."})";
        require(vora::valid_critique(keep), "keep schema rejected");
        require(vora::valid_critique(revise), "revise schema rejected");
        for (const auto& invalid : std::vector<std::string>{
            "This is replacement copy.", "```json\n" + keep + "\n```", "[]", "{}",
            R"({"verdict":"pass","issues":[],"instruction":"Keep"})",
            R"({"verdict":"keep","issues":["An issue"],"instruction":"Keep"})",
            R"({"verdict":"revise","issues":[],"instruction":"Fix"})",
            R"({"verdict":"revise","issues":[42],"instruction":"Fix"})",
            R"({"verdict":"revise","issues":["   "],"instruction":"Fix"})",
            R"({"verdict":"keep","issues":[],"instruction":"\f\u000b"})",
            R"({"verdict":"keep","issues":[],"instruction":"Keep","extra":true})"
            , R"({"verdict":"revise","verdict":"keep","issues":[],"instruction":"Keep"})"
            , R"({"verdict":"keep","issues":["Bad claim"],"issues":[],"instruction":"Keep"})"
        }) require(!vora::valid_critique(invalid), "invalid schema accepted");
        const auto workflow = vora::parse(
            "workflow Checked(input):\nagent a = \"role\"\nreview = a(\"{input}\")\n"
            "final = a(\"{review}\")\nvalidate review as critique\nreturn final\n");
        int calls = 0;
        try {
            vora::run(workflow, "draft", [&](const std::string&, const std::string&) { ++calls; return "replacement copy"; }, {1, 3, 10});
            throw std::runtime_error("invalid review accepted by runtime");
        } catch (const vora::ExecutionError& error) {
            require(calls == 1, "invalid review was retried or sent downstream");
            require(error.events.at(1).value("error", "") == "validation_failed", "missing validation event");
            require(error.events.dump().find("replacement copy") == std::string::npos, "review content leaked to trace");
        }
        calls = 0;
        const auto result = vora::run(workflow, "draft", [&](const std::string&, const std::string& prompt) {
            ++calls;
            if (calls == 1) return revise;
            require(prompt == revise, "validated review not passed verbatim");
            return std::string("Corrected final copy.");
        });
        require(calls == 2 && result.result == "Corrected final copy.", "valid review did not reach editor");
        auto invalid_graph = workflow;
        invalid_graph.validations[0].step = "missing";
        calls = 0;
        try {
            vora::run(invalid_graph, "", [&](const std::string&, const std::string&) { ++calls; return keep; });
            throw std::runtime_error("unknown validation target accepted");
        } catch (const vora::ExecutionError&) { require(calls == 0, "invalid graph called provider"); }
        int factories = 0;
        vora::RunOptions options{1, 1, 4};
        options.provider_for_step = [&](const vora::Step&) -> vora::Provider {
            ++factories;
            return [attempts = 0, keep](const std::string&, const std::string&) mutable {
                if (++attempts == 1) throw vora::TransientError("temporary");
                return keep;
            };
        };
        const auto factory_result = vora::run(workflow, "draft", [](const std::string&, const std::string&) { return std::string("unused"); }, options);
        require(factories == 2 && factory_result.events.back()["calls"] == 4, "factory recreated state on retry");

        const auto repaired = vora::parse(
            "workflow Repair(input):\nagent a = \"role\"\nfinal = a(\"{input}\")\n"
            "require final sentences 2\nrequire final contains \"free\"\n"
            "forbid final contains \"$20\"\nrepair final max 2\nreturn final\n");
        calls = 0;
        std::vector<std::string> prompts;
        const auto repaired_result = vora::run(repaired, "notice", [&](const std::string&, const std::string& prompt) {
            ++calls;
            prompts.push_back(prompt);
            if (calls == 1) return std::string("Admission is $20.");
            return std::string("Admission is free. Everyone is welcome.");
        }, {1, 0, 3});
        require(calls == 2 && repaired_result.result == "Admission is free. Everyone is welcome.",
                "Deterministic repair did not correct output");
        require(prompts[1].find("VORA DETERMINISTIC VALIDATION FAILED") != std::string::npos &&
                prompts[1].find("$20") != std::string::npos && prompts[1].find("free") != std::string::npos,
                "Repair prompt omits exact failed constraints");
        require(repaired_result.events.at(1).at("event") == "step_repair" &&
                repaired_result.events.at(1).at("failed_checks") == 3,
                "Repair event metadata");

        calls = 0;
        try {
            vora::run(repaired, "notice", [&](const std::string&, const std::string&) {
                ++calls;
                return std::string("Still wrong.");
            }, {1, 0, 3});
            throw std::runtime_error("exhausted repair accepted");
        } catch (const vora::ExecutionError& error) {
            require(calls == 3, "repair limit was not enforced");
            require(error.events.back().at("event") == "workflow_failed", "repair failure trace");
        }

        require(vora::sentence_count("What?! Fine... Done.") == 3,
                "Terminal punctuation runs must count once");
        std::cout << "PASS: critique schemas, failure blocks editor without retry, valid data flow, preflight and redaction\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
