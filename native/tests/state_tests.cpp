#include "vora_state.hpp"
#include <iostream>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    require(failed, "Expected failure");
}
int main() {
    const auto workflow = vora::parse("workflow Chain(input):\nagent a = \"role\"\nfirst = a(\"{input}\")\nsecond = a(\"{first}\")\nreturn second\n");
    try {
        // Bounded history, role invalidation and injection into runtime prompts.
        auto memory = vora::empty_memory();
        for (int i = 0; i < 5; ++i) vora::remember(memory, workflow, "task", {{"first", "old"}, {"second", "old"}});
        require(memory["agents"]["a"]["entries"].size() == 4, "History not bounded");
        vora::RunOptions options;
        options.agent_memory = vora::memory_context(memory, workflow);
        int observed = 0;
        vora::run(workflow, "task", [&](const auto&, const std::string& prompt) {
            if (prompt.find("VORA PRIOR RUN CONTEXT") != std::string::npos) ++observed;
            return "done";
        }, options);
        require(observed == 2, "Memory not delivered to agent");
        auto changed = workflow; changed.agents.at("a").role = "new role";
        require(vora::memory_context(memory, changed).empty(), "Role change reused context");
        require(vora::bounded_text("a\xe2\x82\xac", 2) == "a", "UTF-8 truncation corrupts text");
        // Failure saves completed ancestors and reserves the failed call.
        std::map<std::string, std::string> saved;
        int calls = 0;
        options = {};
        options.checkpoint = [&](const auto& outputs, int count) { saved = outputs; calls = count; };
        int invocations = 0;
        rejects([&] { vora::run(workflow, "task", [&](const auto&, const auto&) -> std::string {
            if (++invocations == 2) throw std::runtime_error("offline");
            return "first result";
        }, options); });
        require(saved.size() == 1 && calls == 2, "Failed run checkpoint lost progress or calls");
        options.resume_outputs = saved; options.resume_calls = calls;
        int resumed = 0;
        const auto result = vora::run(workflow, "task", [&](const auto&, const std::string& prompt) {
            ++resumed; require(prompt == "first result", "Dependency was not restored"); return "final";
        }, options);
        require(resumed == 1 && result.result == "final" && calls == 3, "Resume replayed ancestors or reset budget");
        options.resume_outputs = saved; options.resume_calls = calls;
        resumed = 0;
        vora::run(workflow, "task", [&](const auto&, const auto&) { ++resumed; return "bad"; }, options);
        require(resumed == 0, "Completed checkpoint repeated calls");
        options.resume_outputs = {{"second", "orphan"}}; options.resume_calls = 1;
        rejects([&] { vora::run(workflow, "task", [](const auto&, const auto&) { return "bad"; }, options); });
        options.resume_outputs = {{"first", "value"}}; options.resume_calls = 2; options.max_calls = 2;
        rejects([&] { vora::run(workflow, "task", [](const auto&, const auto&) { return "bad"; }, options); });
        // Preserve successful parallel siblings even when the other branch fails.
        const auto parallel = vora::parse("workflow Parallel(input):\nagent a = \"role\"\nleft = a(\"left {input}\")\nright = a(\"right {input}\")\nfinal = a(\"{left} {right}\")\nreturn final\n");
        options = {}; saved.clear(); calls = 0;
        options.checkpoint = [&](const auto& outputs, int count) { saved = outputs; calls = count; };
        std::atomic<bool> left_seen{false};
        rejects([&] { vora::run(parallel, "task", [&](const auto&, const std::string& prompt) -> std::string {
            if (prompt.find("right") == 0) {
                while (!left_seen.load()) std::this_thread::yield();
                throw std::runtime_error("offline");
            }
            left_seen = true;
            return "left done";
        }, options); });
        require(saved.count("left") == 1 && saved.count("right") == 0 && calls == 2, "Parallel sibling checkpoint lost");
        options.resume_outputs = saved; options.resume_calls = calls;
        std::atomic<int> parallel_calls{0};
        vora::run(parallel, "task", [&](const auto&, const auto&) { ++parallel_calls; return "done"; }, options);
        require(parallel_calls == 2 && calls == 4, "Parallel resume repeated completed branch");
        // Persist-before-call failures block external work.
        options = {}; options.checkpoint = [](const auto&, int count) { if (count > 0) throw std::runtime_error("disk full"); };
        invocations = 0;
        rejects([&] { vora::run(workflow, "task", [&](const auto&, const auto&) { ++invocations; return "bad"; }, options); });
        require(invocations == 0, "Provider called without durable reservation");
        // Atomic file replacement, writer exclusion and previous state preservation.
        const auto folder = std::filesystem::temp_directory_path() / ("vora-test-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(folder);
        const auto path = folder / "state.json";
        {
            vora::StateLock lock(path);
            rejects([&] { vora::StateLock second(path); });
            vora::write_state(path, {{"value", 1}});
            vora::write_state(path, {{"value", 2}});
            require(vora::read_state(path)["value"] == 2, "Atomic replacement failed");
            rejects([&] { vora::write_state(path, {{"value", std::string(9 * 1024 * 1024, 'x')}}); });
            require(vora::read_state(path)["value"] == 2, "Failed write damaged state");
        }
        require(!std::filesystem::exists(path.string() + ".lock"), "Lock not released");
        std::filesystem::remove_all(folder);
        std::cout << "State tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
