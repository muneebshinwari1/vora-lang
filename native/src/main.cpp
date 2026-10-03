#include "vora_http.hpp"
#include "vora_state.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using nlohmann::json;

static void save_json(const std::string& path, const json& data) {
    if (path.empty()) return;
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot write output file: " + path);
    file << data.dump(2) << '\n';
    if (!file) throw std::runtime_error("Failed to write output file: " + path);
}

static int integer(const std::string& value) {
    size_t used = 0;
    const int result = std::stoi(value, &used);
    if (used != value.size()) throw std::runtime_error("Expected an integer.");
    return result;
}

static std::string read_input(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read input file.");
    std::string input((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (input.size() > 1024 * 1024) throw std::runtime_error("Input file exceeds 1 MiB.");
    if (input.compare(0, 3, "\xef\xbb\xbf") == 0) input.erase(0, 3);
    return input;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::string trace, output;
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout << "Vora 0.4.0-dev - native C++ agent workflow engine\n"
                      << "vora check FILE\nvora plan FILE [--mermaid]\n"
                      << "vora run FILE --input TEXT [--provider local|demo] [--endpoint URL]\n"
                      << "  Use --input-file FILE instead for saved or multiline text.\n"
                      << "  [--model NAME] [--max-tokens 256] [--reasoning-budget 0] [--workers 4] [--retries 0]\n"
                      << "  [--max-calls 20] [--trace FILE.json] [--output FILE.json]\n"
                      << "  [--memory FILE.json] [--checkpoint FILE.json | --resume FILE.json]\n"
                      << "Default: REAL local model at http://127.0.0.1:18080/v1/chat/completions\n"
                      << "For interactive use, double-click Run Vora.cmd in the native folder.\n";
            return 0;
        }
        if (argc < 3) throw std::runtime_error("A workflow file is required. Use --help.");
        const std::string command = argv[1];
        if (command != "check" && command != "plan" && command != "run") throw std::runtime_error("Unknown command.");
        std::string input, provider_name = "local", endpoint = "http://127.0.0.1:18080/v1/chat/completions", model = "local";
        bool has_input = false, mermaid = false;
        std::string input_path;
        int tokens = 256, reasoning = 0;
        vora::RunOptions limits;
        std::string memory_path, checkpoint_path;
        bool resume = false;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--mermaid" && command == "plan") { mermaid = true; continue; }
            if (command != "run" || i + 1 >= argc) throw std::runtime_error("Unknown or incomplete option: " + option);
            const std::string value = argv[++i];
            if (option == "--input") { input = value; has_input = true; }
            else if (option == "--input-file") { input_path = value; input = read_input(value); has_input = true; }
            else if (option == "--provider") provider_name = value;
            else if (option == "--endpoint") endpoint = value;
            else if (option == "--model") model = value;
            else if (option == "--trace") trace = value;
            else if (option == "--output") output = value;
            else if (option == "--max-tokens") tokens = integer(value);
            else if (option == "--reasoning-budget") reasoning = integer(value);
            else if (option == "--workers") limits.workers = integer(value);
            else if (option == "--retries") limits.retries = integer(value);
            else if (option == "--max-calls") limits.max_calls = integer(value);
            else if (option == "--memory") memory_path = value;
            else if (option == "--checkpoint") {
                if (!checkpoint_path.empty()) throw std::runtime_error("Specify checkpoint or resume once.");
                checkpoint_path = value;
            } else if (option == "--resume") {
                if (!checkpoint_path.empty()) throw std::runtime_error("Specify checkpoint or resume once.");
                checkpoint_path = value;
                resume = true;
            }
            else throw std::runtime_error("Unknown option: " + option);
        }
        const auto workflow = vora::load(argv[2]);
        if (command == "check") {
            std::cout << "Valid: " << workflow.name << ", " << workflow.steps.size() << " steps\n";
            return 0;
        }
        if (command == "plan") {
            if (!mermaid) std::cout << vora::plan(workflow).dump(2) << '\n';
            else {
                std::map<std::string, size_t> ids;
                for (size_t i = 0; i < workflow.steps.size(); ++i) ids[workflow.steps[i].name] = i;
                std::cout << "flowchart TD\n";
                for (const auto& step : workflow.steps) {
                    std::cout << "  s" << ids.at(step.name) << "[\"" << step.name << " / " << step.agent << "\"]\n";
                    for (const auto& dep : step.dependencies) std::cout << "  s" << ids.at(dep) << " --> s" << ids.at(step.name) << '\n';
                }
            }
            return 0;
        }
        if (!has_input) throw std::runtime_error("--input is required.");
        vora::Provider provider;
        if (provider_name == "local") {
            provider = vora::LocalProvider(endpoint, model, tokens, reasoning);
            limits.provider_for_step = [endpoint, model, tokens, reasoning, &workflow](const vora::Step& step) -> vora::Provider {
                bool critique = false;
                for (const auto& rule : workflow.validations) if (rule.step == step.name && rule.kind == "critique") critique = true;
                return vora::LocalProvider(endpoint, model, tokens, reasoning, critique);
            };
            std::cerr << "REAL local model inference; " << workflow.steps.size() << " steps.\n";
        } else if (provider_name == "demo") {
            std::cerr << "SIMULATED DEMO: no AI inference.\n";
            provider = [](const std::string& role, const std::string& prompt) {
                return "[SIMULATED DEMO] " + role + ": " + prompt.substr(0, 240);
            };
        } else throw std::runtime_error("Provider must be local or demo.");
        // Include temporary/lock sidecars in collision checks.
        auto normalized_path = [](const std::string& path) {
            auto value = std::filesystem::weakly_canonical(path).generic_string();
#ifdef _WIN32
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return value;
        };
        std::set<std::string> protected_paths;
        for (const auto& path : {std::string(argv[2]), input_path, trace, output})
            if (!path.empty()) protected_paths.insert(normalized_path(path));
        for (const auto& path : {memory_path, checkpoint_path}) {
            if (path.empty()) continue;
            for (const auto& reserved : {path, path + ".tmp", path + ".lock"})
                if (!protected_paths.insert(normalized_path(reserved)).second)
                    throw std::runtime_error("State files and sidecars must have distinct paths from other files.");
        }
        std::unique_ptr<vora::StateLock> memory_lock, checkpoint_lock;
        nlohmann::json memory = vora::empty_memory();
        if (!memory_path.empty()) {
            memory_lock = std::make_unique<vora::StateLock>(memory_path);
            if (std::filesystem::exists(memory_path)) memory = vora::read_state(memory_path);
            limits.agent_memory = vora::memory_context(memory, workflow);
        }
        nlohmann::json identity = {{"workflow", vora::workflow_identity(workflow)}, {"input", input},
            {"provider", provider_name}, {"endpoint", endpoint}, {"model", model},
            {"tokens", tokens}, {"reasoning", reasoning}, {"retries", limits.retries},
            {"max_calls", limits.max_calls}, {"memory_enabled", !memory_path.empty()}};
        nlohmann::json checkpoint;
        if (!checkpoint_path.empty()) {
            checkpoint_lock = std::make_unique<vora::StateLock>(checkpoint_path);
            if (resume) {
                checkpoint = vora::read_state(checkpoint_path);
                if (checkpoint.at("format") != "vora-checkpoint" || checkpoint.at("version") != 1 ||
                    checkpoint.at("identity") != identity)
                    throw std::runtime_error("Checkpoint does not match workflow, input or provider settings.");
                if (!checkpoint.at("calls").is_number_integer() ||
                    !checkpoint.at("transaction").is_string() || checkpoint.at("transaction").get<std::string>().empty())
                    throw std::runtime_error("Malformed checkpoint metadata.");
                limits.resume_outputs = checkpoint.at("outputs").get<std::map<std::string, std::string>>();
                limits.resume_calls = checkpoint.at("calls").get<int>();
                limits.agent_memory = checkpoint.at("memory_context").get<std::map<std::string, std::string>>();
            } else {
                if (std::filesystem::exists(checkpoint_path))
                    throw std::runtime_error("Checkpoint already exists. Use --resume or choose a new path.");
                checkpoint = {{"format", "vora-checkpoint"}, {"version", 1}, {"identity", identity},
                    {"memory_context", limits.agent_memory}, {"outputs", nlohmann::json::object()}, {"calls", 0},
                    {"transaction", std::to_string(std::chrono::system_clock::now().time_since_epoch().count())}};
            }
            limits.checkpoint = [&](const auto& outputs, int calls) {
                checkpoint["outputs"] = outputs;
                checkpoint["calls"] = calls;
                vora::write_state(checkpoint_path, checkpoint);
            };
        }
        const auto result = vora::run(workflow, input, provider, limits);
        if (!memory_path.empty()) {
            const std::string transaction = checkpoint_path.empty() ? "" : checkpoint.at("transaction").get<std::string>();
            auto committed = memory.value("committed", nlohmann::json::array());
            if (!committed.is_array() || committed.size() > 64) throw std::runtime_error("Invalid memory transactions.");
            const bool already_saved = !transaction.empty() && std::find(committed.begin(), committed.end(), transaction) != committed.end();
            if (!already_saved) {
                vora::remember(memory, workflow, input, result.outputs);
                if (!transaction.empty()) {
                    committed.push_back(transaction);
                    while (committed.size() > 64) committed.erase(committed.begin());
                    memory["committed"] = committed;
                }
                vora::write_state(memory_path, memory);
            }
        }
        save_json(trace, result.events);
        save_json(output, {{"workflow", workflow.name}, {"provider", provider_name}, {"result", result.result}, {"outputs", result.outputs}, {"events", result.events}});
        std::cout << result.result << '\n';
        return 0;
    } catch (const vora::ExecutionError& error) {
        try { save_json(trace, error.events); } catch (...) { std::cerr << "Could not write failure trace.\n"; }
        std::cerr << "Execution failed: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
