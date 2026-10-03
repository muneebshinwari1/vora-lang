#include "vora_state.hpp"
#include <atomic>
#include <condition_variable>
#include <iostream>

using nlohmann::json;
static const std::string keep_critique = R"({"verdict":"keep","issues":[],"instruction":"Keep the draft."})";
static const std::string revise = R"({"verdict":"revise","issues":["Needs revision"],"instruction":"Improve the draft."})";
static void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class F> static void fails(F operation) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
static vora::Workflow review_workflow(int rounds = 3) {
    return vora::parse("workflow Review(input):\nagent writer = \"writer\"\nagent critic = \"critic\"\nanswer = writer(\"Task {input}\")\nreview answer with critic max " + std::to_string(rounds) + "\nreturn answer\n");
}
static vora::Workflow route_workflow() {
    return vora::parse(R"VORA(workflow Route(input):
agent writer = "writer"
agent critic = "critic"
draft = writer("{input}")
critique = critic("Check {draft}")
validate critique as critique
final = writer("Revise {draft}: {critique}") when critique verdict "revise" else draft
return final
)VORA");
}
static void routing() {
    for (bool change : {false, true}) {
        int writes = 0, calls = 0;
        auto workflow = route_workflow();
        vora::RunOptions options; options.workers = 1; options.max_calls = change ? 3 : 2;
        auto result = vora::run(workflow, "task", [&](const auto& role, const auto&) {
            ++calls;
            if (role == "critic") return change ? revise : keep_critique;
            return ++writes == 1 ? std::string("draft") : std::string("revised");
        }, options);
        require(result.result == (change ? "revised" : "draft") && calls == (change ? 3 : 2), "Route called wrong branch");
        auto memory = vora::empty_memory();
        vora::remember(memory, workflow, "task", result.outputs);
        require(memory["agents"]["writer"]["entries"].size() == static_cast<size_t>(change ? 2 : 1), "Skipped agent received phantom memory");
        require(vora::plan(workflow)["minimum_calls"] == 2, "Route minimum is wrong");
        options.resume_outputs = result.outputs; options.resume_calls = calls;
        auto restored = vora::run(workflow, "task", [](const auto&, const auto&) -> std::string { throw std::runtime_error("Completed route called provider"); }, options);
        require(restored.result == result.result, "Completed alias resume failed");
        if (!change) {
            options.resume_outputs["final"] = "forged";
            fails([&] { vora::run(workflow, "task", [](const auto&, const auto&) { return "bad"; }, options); });
        }
    }
    fails([&] { vora::run(route_workflow(), "x", [](const auto&, const auto&) { return "malformed"; }); });
}
static void review_loop() {
    int writers = 0, critics = 0;
    auto workflow = review_workflow();
    const auto result = vora::run(workflow, "x", [&](const auto& role, const auto& prompt) {
        if (role == "critic") return ++critics == 1 ? revise : keep_critique;
        if (++writers == 2) require(prompt.find("Needs revision") != std::string::npos && prompt.find("draft1") != std::string::npos, "Revision lost feedback");
        return "draft" + std::to_string(writers);
    });
    require(result.result == "draft2" && writers == 2 && critics == 2, "Review loop did not stop at acceptance");
    require(vora::plan(workflow)["minimum_calls"] == 2, "Review minimum is wrong");
    require(result.events.dump().find("review_accepted") != std::string::npos, "Missing acceptance trace");
    writers = critics = 0;
    fails([&] { vora::run(review_workflow(2), "x", [&](const auto& role, const auto&) {
        if (role == "critic") { ++critics; return revise; } ++writers; return std::string("draft");
    }); });
    require(writers == 2 && critics == 2, "Round limit was exceeded");
    vora::RunOptions limited; limited.max_calls = 3;
    int calls = 0;
    fails([&] { vora::run(workflow, "x", [&](const auto& role, const auto&) { ++calls; return role == "critic" ? revise : std::string("draft"); }, limited); });
    require(calls == 3, "Review exceeded global budget");
    fails([&] { vora::run(workflow, "x", [](const auto& role, const auto&) { return role == "critic" ? std::string("not JSON") : std::string("draft"); }); });
    int retries = 0;
    vora::RunOptions retry_options; retry_options.retries = 1;
    auto recovered = vora::run(review_workflow(1), "x", [&](const auto& role, const auto&) {
        if (role == "critic" && retries++ == 0) throw vora::TransientError("temporary");
        return role == "critic" ? keep_critique : std::string("draft");
    }, retry_options);
    require(recovered.result == "draft" && retries == 2, "Reviewer retry lost the draft or changed round count");
    int bounded_calls = 0;
    fails([&] { vora::run(workflow, "x", [&](const auto&, const auto&) {
        ++bounded_calls; return std::string(4 * 1024 * 1024, 'x');
    }); });
    require(bounded_calls == 1, "Oversized reviewer prompt was sent to provider");
}
static void stage_resume() {
    for (const std::string phase : {"critique", "draft", "accepted"}) {
        auto workflow = review_workflow();
        json graph; std::map<std::string, std::string> outputs; int reservations = 0, writes = 0, reads = 0;
        vora::RunOptions options;
        options.graph_checkpoint = [&](const auto& out, int calls, const json& state) {
            graph = state; outputs = out; reservations = calls;
            if (state.contains("answer") && state["answer"]["phase"] == phase)
                throw vora::ExecutionError("Simulated crash after durable stage");
        };
        fails([&] { vora::run(workflow, "x", [&](const auto& role, const auto&) {
            if (role == "critic") { ++reads; return phase == "draft" ? revise : keep_critique; }
            ++writes; return std::string("saved draft");
        }, options); });
        require(writes == 1 && reservations == (phase == "critique" ? 1 : 2), "Unexpected crash reservation");
        vora::RunOptions resumed; resumed.resume_graph = graph; resumed.resume_outputs = outputs; resumed.resume_calls = reservations;
        auto result = vora::run(workflow, "x", [&](const auto& role, const auto&) {
            if (role == "critic") { ++reads; return keep_critique; } ++writes; return std::string("new draft");
        }, resumed);
        require(writes == (phase == "draft" ? 2 : 1), "Resume repeated completed draft");
        require(reads == (phase == "draft" ? 2 : 1), "Resume repeated completed critic");
        require(result.result == (phase == "draft" ? "new draft" : "saved draft"), "Resume result mismatch");
        resumed.resume_outputs = result.outputs;
        // The accepted stage is mandatory for a completed reviewed output.
        resumed.resume_graph = json::object();
        fails([&] { vora::run(workflow, "x", [](const auto&, const auto&) { return "bad"; }, resumed); });
    }
    vora::RunOptions invalid;
    invalid.resume_graph = {{"answer", {{"round", 11}, {"phase", "accepted"}, {"draft", "x"}, {"critique", keep_critique}}}};
    invalid.resume_calls = 20;
    fails([&] { vora::run(review_workflow(), "x", [](const auto&, const auto&) { return "bad"; }, invalid); });
}
static void graph_parallel() {
    const auto workflow = vora::parse(R"VORA(workflow Parallel(input):
agent writer = "writer"
agent critic = "critic"
left = writer("left {input}")
right = writer("right {input}")
review left with critic max 2
review right with critic max 2
joined = writer("join {left} {right}")
return joined
)VORA");
    std::mutex mutex; std::condition_variable cv; int arrivals = 0;
    json graph; int saved_calls = 0;
    vora::RunOptions options; options.workers = 2;
    options.graph_checkpoint = [&](const auto&, int calls, const json& state) { graph = state; saved_calls = calls; };
    auto result = vora::run(workflow, "x", [&](const auto& role, const auto& prompt) {
        if (role == "critic") return keep_critique;
        if (prompt.rfind("join", 0) == 0) return std::string("joined");
        std::unique_lock<std::mutex> lock(mutex); ++arrivals; cv.notify_all();
        require(cv.wait_for(lock, std::chrono::seconds(2), [&] { return arrivals == 2; }), "Reviewed branches failed to overlap");
        return std::string("draft");
    }, options);
    require(result.result == "joined" && saved_calls == 5 && graph.size() == 2, "Parallel review checkpoint corrupted");
    options.resume_calls = saved_calls; options.resume_outputs = result.outputs; options.resume_graph = graph;
    auto restored = vora::run(workflow, "x", [](const auto&, const auto&) -> std::string { throw std::runtime_error("Repeated complete graph"); }, options);
    require(restored.result == "joined", "Completed parallel graph resume failed");
}
static void invalid_source() {
    const std::string prefix = "workflow Invalid(input):\nagent a = \"role\"\nx = a(\"{input}\")\n";
    for (const auto& statement : {"review x with a max 0", "review x with a max 11", "review x with missing max 2", "review missing with a max 2", "review x with a max 2\nreview x with a max 3", "y = a(\"{input}\") when x verdict \"keep\" else x"})
        fails([&] { vora::parse(prefix + statement + "\nreturn x\n"); });
    auto changed = review_workflow(); auto old = vora::workflow_identity(changed); changed.reviews[0].max_rounds = 2;
    require(old != vora::workflow_identity(changed), "Review limits missing from identity");
    auto route = route_workflow(); old = vora::workflow_identity(route); route.steps.back().verdict = "keep";
    require(old != vora::workflow_identity(route), "Routing missing from identity");
}
int main() {
    try { routing(); review_loop(); stage_resume(); graph_parallel(); invalid_source();
        std::cout << "PASS: conditional budgets, bounded reviews, durable stage resume, parallel joins and graph identity\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
