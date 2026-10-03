#include "vora_tools.hpp"
#include "vora_state.hpp"
#include <iostream>
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    require(failed, "Expected rejection");
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("vora-tools-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        std::ofstream(root / "notes.txt", std::ios::binary) << "alpha beta\nalpha gamma\n";
        std::ofstream(root / "empty.txt", std::ios::binary);
        std::ofstream(root / "large.txt", std::ios::binary) << std::string(256 * 1024 + 1, 'x');
        vora::ToolRegistry tools(root.string(), vora::builtin_tools());
        require(tools("read_file", {{"path", "notes.txt"}}) == "alpha beta\nalpha gamma\n", "Wrong file content");
        require(tools("read_file", {{"path", "empty.txt"}}).empty(), "Empty file unsupported");
        const auto stats = nlohmann::json::parse(tools("text_stats", {{"text", "a b\nc"}}));
        require(stats["words"] == 3 && stats["lines"] == 2, "Wrong text stats");
        const auto search = nlohmann::json::parse(tools("search_file", {{"path", "notes.txt"}, {"query", "alpha"}, {"max_matches", 1}}));
        require(search["matches"].size() == 1 && search["truncated"] == true, "Search not bounded");
        require(tools("json_select", {{"text", "{\"a\":[7]}"}, {"pointer", "/a/0"}}) == "7", "JSON pointer wrong");
        require(nlohmann::json::parse(tools("list_files", nlohmann::json::object()))["entries"].size() == 3, "List wrong");
        rejects([&] { tools("read_file", {{"path", "../notes.txt"}}); });
        rejects([&] { tools("read_file", {{"path", (root / "notes.txt").string()}}); });
        rejects([&] { tools("read_file", {{"path", "notes.txt:stream"}}); });
        rejects([&] { tools("read_file", {{"path", "large.txt"}}); });
        rejects([&] { tools("read_file", {{"path", "."}}); });
        rejects([&] { tools("read_file", {{"path", "notes.txt"}, {"unknown", true}}); });
        rejects([&] { tools("search_file", {{"path", "notes.txt"}, {"query", "alpha"}, {"max_matches", 1.5}}); });
        rejects([&] { tools("json_select", {{"text", "{}"}, {"pointer", "/missing"}}); });
        vora::ToolRegistry denied(root.string(), {"text_stats"});
        rejects([&] { denied("read_file", {{"path", "notes.txt"}}); });
        vora::ToolRegistry no_root("", {"read_file"});
        rejects([&] { no_root("read_file", {{"path", "notes.txt"}}); });
        std::error_code error;
        std::filesystem::create_symlink(root / "notes.txt", root / "link.txt", error);
        if (!error) rejects([&] { tools("read_file", {{"path", "link.txt"}}); });
        else std::cout << "Symlink fixture unavailable on this host\n";
        rejects([] { vora::tool_arguments("read_file", "{\"path\":\"a\",\"path\":\"b\"}"); });
        rejects([] { vora::tool_arguments("read_file", "{\"path\":7}"); });
        rejects([] { vora::parse("workflow T(input):\ntool t = \"shell\"\nx = t(\"{}\")\nreturn x\n"); });
        const auto source = R"VORA(workflow Tools(input):
tool stats = "text_stats"
agent report = "Summarize counts."
data = stats("{\"text\":\"{input}\"}")
final = report("Counts: {data}")
return final
)VORA";
        const auto workflow = vora::parse(source);
        require(workflow.steps[0].kind == "tool" && vora::plan(workflow)["steps"][0]["builtin"] == "text_stats", "Tool not in plan");
        vora::RunOptions options;
        options.tool_executor = [&](const auto& name, const auto& args) { return tools(name, args); };
        std::map<std::string, std::string> saved;
        int calls = 0;
        options.checkpoint = [&](const auto& outputs, int count) { saved = outputs; calls = count; };
        int model_calls = 0;
        const auto input = std::string("quotes \", \"path\": \"../secret\" {unexpanded}");
        const auto result = vora::run(workflow, input, [&](const auto&, const std::string& prompt) {
            ++model_calls;
            require(prompt.find("Counts:") == 0, "Tool dependency not provided");
            return "report";
        }, options);
        require(model_calls == 1 && calls == 2, "Tool call budget incorrect");
        require(nlohmann::json::parse(result.outputs.at("data"))["bytes"] == input.size(), "Unsafe JSON interpolation");
        require(std::any_of(result.events.begin(), result.events.end(), [](const auto& e) { return e["event"] == "tool_completed"; }), "Missing tool trace");
        options.resume_outputs = saved; options.resume_calls = calls;
        options.tool_executor = [](const auto&, const auto&) -> std::string { throw std::runtime_error("Must not replay tool"); };
        model_calls = 0;
        vora::run(workflow, input, [&](const auto&, const auto&) { ++model_calls; return "bad"; }, options);
        require(model_calls == 0, "Completed tools were replayed");
        // Empty tool output is a valid checkpoint value and memory ignores tool steps.
        const auto empty = vora::parse(R"VORA(workflow Empty(input):
tool read = "read_file"
x = read("{\"path\":\"empty.txt\"}")
return x
)VORA");
        options = {}; options.tool_executor = [&](const auto& name, const auto& args) { return tools(name, args); };
        const auto empty_result = vora::run(empty, "", {}, options);
        require(empty_result.result.empty(), "Empty tool output rejected");
        auto memory = vora::empty_memory(); vora::remember(memory, empty, "", empty_result.outputs);
        require(memory["agents"].empty(), "Tool output was stored as agent memory");
        options.resume_outputs = empty_result.outputs; options.resume_calls = 1;
        vora::run(empty, "", {}, options);
        std::cout << "Tool tests passed\n";
        std::filesystem::remove_all(root);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; std::filesystem::remove_all(root); return 1; }
}
