#include "vora_state.hpp"
#include <iostream>

static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> static void rejects(F fn) { bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; } check(failed, "Expected rejection"); }
int main() {
    try {
        const std::string prefix = "workflow Human(input):\nagent writer = \"role\"\ndraft = writer(\"{input}\")\n";
        const auto workflow = vora::parse(prefix + "approval gate = draft \"Literal {missing} instructions\"\nreturn gate");
        check(vora::plan(workflow)["minimum_calls"] == 1, "Gate consumed planned calls");
        check(vora::plan(workflow)["steps"][1]["kind"] == "approval", "Missing gate plan");
        int calls = 0;
        vora::Provider provider = [&](const auto&, const auto&) { ++calls; return std::string("draft"); };
        rejects([&] { vora::run(workflow, "x", provider); });
        check(calls == 0, "Missing durable callback invoked provider");
        vora::RunOptions options;
        options.durable_checkpoint = [&](const auto& outputs, int count, const auto& graph, const auto& approvals) {
            options.resume_outputs = outputs; options.resume_calls = count;
            options.resume_graph = graph; options.resume_approvals = approvals;
        };
        bool paused = false;
        try { vora::run(workflow, "x", provider, options); }
        catch (const vora::WorkflowPaused& pause) { paused = true; check(pause.events.back()["event"] == "workflow_paused", "Pause reported as failure"); }
        check(paused && calls == 1, "Gate did not pause");
        const auto id = options.resume_approvals["gate"]["request_id"].get<std::string>();
        vora::decide_approval(options.resume_approvals, "gate", id, "approve", "operator", "Reviewed");
        auto result = vora::run(workflow, "x", provider, options);
        check(result.result == "draft" && calls == 1, "Approved alias called provider");
        for (const auto& body : {"approval gate = missing \"Review\"\nreturn gate", "approval gate = gate \"Review\"\nreturn gate",
            "approval gate = draft \"\"\nreturn gate", "approval gate = draft \"Review\"\nrequire gate contains \"draft\"\nrepair gate max 1\nreturn gate",
            "approval gate = draft \"Review\"\nreview gate with writer max 1\nreturn gate"})
            rejects([&] { vora::parse(prefix + body); });
        auto forged = options;
        forged.resume_approvals = nlohmann::json::object();
        rejects([&] { vora::run(workflow, "x", provider, forged); });
        auto changed = workflow; changed.steps[1].prompt = "Another instruction";
        check(vora::workflow_identity(changed) != vora::workflow_identity(workflow), "Approval instructions absent from identity");
        changed = workflow; changed.steps[1].dependencies.clear();
        rejects([&] { vora::run(changed, "x", provider, options); });
        std::cout << "PASS: approval parsing, plans, durable pause, zero-call release, identity and malformed-state rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
