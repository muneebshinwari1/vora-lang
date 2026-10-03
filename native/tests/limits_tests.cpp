#include "vora_runtime.hpp"
#include <iostream>
#include <cstdlib>
#include <new>

static bool limit_allocations = false;
void* operator new(std::size_t bytes) {
    if (limit_allocations && bytes > 8 * 1024 * 1024) throw std::bad_alloc();
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F operation) {
    try { operation(); } catch (const vora::ExecutionError&) { return; }
    throw std::runtime_error("Expected bounded execution failure");
}

int main() {
    try {
        // A large replacement must be rejected before copying any of it.
        const std::map<std::string, std::string> large{{"input", std::string(1024 * 1024, 'x')}};
        rejects([&] { vora::interpolate("{input}{input}", large, 16); });
        require(vora::interpolate("{x}{x}", {{"x", "1234"}}, 8) == "12341234", "Exact expansion boundary rejected");
        rejects([&] { vora::interpolate("{x}{x}!", {{"x", "1234"}}, 8); });
        std::string bounded = "1234";
        rejects([&] { vora::append_prompt(bounded, large.at("input"), 0, large.at("input").size(), 8); });
        require(bounded == "1234", "Rejected append mutated prompt");
        std::string repeated;
        for (int i = 0; i < 1000; ++i) repeated += "{input}";
        // The old implementation attempts allocations above 8 MiB before its
        // late size check. The guard prevents an actual gigabyte allocation.
        limit_allocations = true;
        try { rejects([&] { vora::interpolate(repeated, large); }); }
        catch (...) { limit_allocations = false; throw; }
        limit_allocations = false;
        std::string nested = std::string(100, '[') + "0" + std::string(100, ']');
        require(!vora::valid_critique(nested), "Deep critique was accepted");
        const auto one = vora::parse("workflow One(input):\nagent a = \"role\"\nx = a(\"{input}\")\nreturn x\n");
        int calls = 0;
        const auto provider = [&](const auto&, const auto&) { ++calls; return "ok"; };
        rejects([&] { vora::run(one, std::string(1024 * 1024 + 1, 'x'), provider); });
        require(calls == 0, "Oversized input invoked provider");
        vora::RunOptions options;
        options.max_calls = 10001;
        rejects([&] { vora::run(one, "task", provider, options); });
        require(calls == 0, "Invalid budget invoked provider");

        const auto chain = vora::parse("workflow Chain(input):\nagent a = \"role\"\nx = a(\"{input}\")\ny = a(\"{x}\")\nreturn y\n");
        calls = 0;
        rejects([&] { vora::run(chain, "task", [&](const auto&, const auto&) {
            ++calls; return std::string(4 * 1024 * 1024 + 1, 'x');
        }); });
        require(calls == 1, "Oversized output reached downstream provider");

        const auto expansion = vora::parse("workflow Expand(input):\nagent a = \"role\"\nx = a(\"{input}\")\ny = a(\"{x}{x}{x}{x}{x}\")\nreturn y\n");
        calls = 0;
        rejects([&] { vora::run(expansion, "task", [&](const auto&, const auto&) {
            ++calls; return std::string(1024 * 1024, 'x');
        }); });
        require(calls == 1, "Oversized expanded prompt invoked provider");

        const auto full_prompt = vora::parse("workflow Full(input):\nagent a = \"role\"\nx = a(\"{input}{input}{input}{input}\")\nreturn x\n");
        calls = 0; options = {}; options.agent_memory["a"] = "saved context";
        rejects([&] { vora::run(full_prompt, large.at("input"), provider, options); });
        require(calls == 0, "Memory overflow invoked provider");
        const auto repair_limit = vora::parse("workflow Repair(input):\nagent a = \"role\"\nx = a(\"{input}{input}{input}{input}\")\nrequire x contains \"required\"\nrepair x max 1\nreturn x\n");
        options = {}; calls = 0;
        rejects([&] { vora::run(repair_limit, large.at("input"), provider, options); });
        require(calls == 1, "Repair overflow reached a second provider call");
        const auto tool_limit = vora::parse("workflow Tool(input):\ntool stats = \"text_stats\"\nx = stats({\"text\":\"{input}\"})\nreturn x\n");
        options = {}; calls = 0;
        options.tool_executor = [&](const auto&, const auto&) { ++calls; return "ok"; };
        rejects([&] { vora::run(tool_limit, large.at("input"), {}, options); });
        require(calls == 0, "Oversized tool field reached executor");

        // Repeat independent branches plus join across worker limits. Assertions
        // check budgets, no missing results and no leaked state between runs.
        const auto graph = vora::parse("workflow Graph(input):\nagent a = \"role\"\nleft = a(\"left {input}\")\nright = a(\"right {input}\")\njoined = a(\"{left} {right}\")\nreturn joined\n");
        for (int i = 0; i < 200; ++i) {
            options = {}; options.workers = i % 4 + 1; options.max_calls = 3;
            std::atomic<int> observed{0};
            const auto result = vora::run(graph, std::to_string(i), [&](const auto&, const std::string& prompt) {
                ++observed; return prompt;
            }, options);
            require(observed == 3 && result.outputs.size() == 3, "Repeated run lost results or repeated calls");
            require(result.result == "left " + std::to_string(i) + " right " + std::to_string(i), "Cross-run data contamination");
            require(result.events.back().at("calls") == 3, "Call accounting drift");
        }
        std::cout << "Limits and 200 repeated graph runs passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
