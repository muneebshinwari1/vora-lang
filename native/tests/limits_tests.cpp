#include "vora_runtime.hpp"
#include <iostream>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F operation) {
    try { operation(); } catch (const vora::ExecutionError&) { return; }
    throw std::runtime_error("Expected bounded execution failure");
}

int main() {
    try {
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
