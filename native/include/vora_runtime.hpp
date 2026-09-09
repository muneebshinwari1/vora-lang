#pragma once
#include "vora_parser.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

namespace vora {
using Provider = std::function<std::string(const std::string&, const std::string&)>;
struct RunOptions {
    int workers = 4, retries = 0, max_calls = 20;
    std::function<Provider(const Step&)> provider_for_step{};
};
struct RunResult { std::map<std::string, std::string> outputs; std::string result; nlohmann::json events; };
class TransientError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class ValidationError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class ExecutionError : public std::runtime_error {
public:
    nlohmann::json events = nlohmann::json::array();
    explicit ExecutionError(const std::string& message) : std::runtime_error(message) {}
};

inline bool has_text(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) == 0; });
}

inline bool valid_critique(const std::string& text) {
    bool duplicate_key = false;
    std::vector<std::set<std::string>> object_keys;
    auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& item) {
        if (event == nlohmann::json::parse_event_t::object_start) object_keys.emplace_back();
        else if (event == nlohmann::json::parse_event_t::key && !object_keys.empty()) {
            if (!object_keys.back().insert(item.get<std::string>()).second) duplicate_key = true;
        } else if (event == nlohmann::json::parse_event_t::object_end && !object_keys.empty()) object_keys.pop_back();
        return true;
    };
    const auto value = nlohmann::json::parse(text, callback, false);
    if (duplicate_key) return false;
    if (!value.is_object() || value.size() != 3 || !value.contains("verdict") || !value.contains("issues") || !value.contains("instruction")) return false;
    if (!value["verdict"].is_string() || !value["issues"].is_array() || !value["instruction"].is_string()) return false;
    const auto verdict = value["verdict"].get<std::string>();
    if (verdict != "keep" && verdict != "revise") return false;
    if (!has_text(value["instruction"].get<std::string>())) return false;
    for (const auto& issue : value["issues"])
        if (!issue.is_string() || !has_text(issue.get<std::string>())) return false;
    return (verdict == "keep") == value["issues"].empty();
}

inline int sentence_count(const std::string& text) {
    int count = 0;
    bool terminal_run = false;
    for (const unsigned char c : text) {
        const bool terminal = c == '.' || c == '!' || c == '?';
        if (terminal && !terminal_run) ++count;
        terminal_run = terminal;
    }
    return count;
}

inline std::vector<std::string> validation_failures(
    const Workflow& workflow, const std::string& step, const std::string& response) {
    std::vector<std::string> failures;
    for (const auto& rule : workflow.validations) {
        if (rule.step != step) continue;
        if (rule.kind == "critique" && !valid_critique(response))
            failures.push_back("Return exactly the required critique JSON object.");
        else if (rule.kind == "contains" && response.find(rule.value) == std::string::npos)
            failures.push_back("Include this exact required text: " + nlohmann::json(rule.value).dump());
        else if (rule.kind == "not_contains" && response.find(rule.value) != std::string::npos)
            failures.push_back("Remove this forbidden text: " + nlohmann::json(rule.value).dump());
        else if (rule.kind == "sentences" && sentence_count(response) != rule.number)
            failures.push_back("Return exactly " + std::to_string(rule.number) + " sentence(s), counted by terminal punctuation.");
    }
    return failures;
}

inline std::string interpolate(const std::string& source, const std::map<std::string, std::string>& values) {
    static const std::regex placeholder(R"(\{([A-Za-z_][A-Za-z0-9_]*)\})");
    std::string output;
    size_t end = 0;
    for (std::sregex_iterator it(source.begin(), source.end(), placeholder), last; it != last; ++it) {
        output.append(source, end, static_cast<size_t>(it->position()) - end);
        const auto found = values.find((*it)[1].str());
        if (found == values.end()) throw ExecutionError("Unresolved workflow reference.");
        output += found->second;
        end = static_cast<size_t>(it->position() + it->length());
    }
    output.append(source, end, std::string::npos);
    return output;
}

inline RunResult run(const Workflow& workflow, const std::string& input, Provider provider, RunOptions options = {}) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    static std::atomic<unsigned long long> run_counter{0};
    const auto run_id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" + std::to_string(++run_counter);
    nlohmann::json events = nlohmann::json::array();
    std::mutex event_mutex, call_mutex;
    std::atomic<bool> stopped{false};
    int calls = 0;
    auto emit = [&](const std::string& kind, nlohmann::json fields = nlohmann::json::object()) {
        fields["event"] = kind;
        fields["workflow"] = workflow.name;
        fields["run_id"] = run_id;
        fields["elapsed_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::lock_guard<std::mutex> guard(event_mutex);
        events.push_back(std::move(fields));
    };
    try {
        if (options.workers < 1 || options.workers > 64 || options.retries < 0 || options.retries > 100 || options.max_calls < 1)
            throw ExecutionError("Invalid limits: workers 1..64, retries 0..100, max-calls >= 1.");
        if (!provider) throw ExecutionError("Provider is required.");
        if (workflow.steps.empty()) throw ExecutionError("Workflow must contain steps.");
        std::set<std::string> names;
        for (const auto& step : workflow.steps) {
            if (!names.insert(step.name).second || step.name == workflow.input_name)
                throw ExecutionError("Duplicate step or input name.");
            if (!workflow.agents.count(step.agent)) throw ExecutionError("Unknown agent.");
        }
        if (!names.count(workflow.output)) throw ExecutionError("Undefined return step.");
        std::set<std::pair<std::string, std::string>> validated;
        for (const auto& rule : workflow.validations) {
            const bool supported = rule.kind == "critique" || rule.kind == "contains" ||
                                   rule.kind == "not_contains" || rule.kind == "sentences";
            const auto signature = std::make_pair(rule.step, rule.kind + "\n" + rule.value + "\n" + std::to_string(rule.number));
            if (!names.count(rule.step) || !supported || !validated.insert(signature).second)
                throw ExecutionError("Invalid or duplicate output validation rule.");
        }
        std::map<std::string, int> repair_limits;
        for (const auto& repair : workflow.repairs) {
            if (!names.count(repair.step) || repair.max_attempts < 1 || repair.max_attempts > 5 ||
                !repair_limits.emplace(repair.step, repair.max_attempts).second)
                throw ExecutionError("Invalid or duplicate repair rule.");
            if (std::none_of(workflow.validations.begin(), workflow.validations.end(),
                             [&](const Validation& rule) { return rule.step == repair.step; }))
                throw ExecutionError("Repair target has no validation rule.");
        }
        std::set<std::string> visited;
        static const std::regex reference(R"(\{([A-Za-z_][A-Za-z0-9_]*)\})");
        for (const auto& step : workflow.steps) {
            std::set<std::string> refs;
            for (std::sregex_iterator it(step.prompt.begin(), step.prompt.end(), reference), last; it != last; ++it)
                if ((*it)[1].str() != workflow.input_name) refs.insert((*it)[1].str());
            if (refs != std::set<std::string>(step.dependencies.begin(), step.dependencies.end()))
                throw ExecutionError("Declared dependencies do not match prompt references.");
            for (const auto& dep : refs) if (!names.count(dep)) throw ExecutionError("Undefined dependency.");
        }
        while (visited.size() < names.size()) {
            const auto before = visited.size();
            for (const auto& step : workflow.steps) {
                bool ready = true;
                for (const auto& dep : step.dependencies) if (!visited.count(dep)) ready = false;
                if (ready) visited.insert(step.name);
            }
            if (before == visited.size()) throw ExecutionError("Dependency cycle.");
        }
        if (workflow.steps.size() > static_cast<size_t>(options.max_calls))
            throw ExecutionError("Call limit is below the workflow minimum call count.");

        std::map<std::string, std::string> outputs;
        std::set<std::string> pending = names;
        auto execute = [&](const Step& step, const std::string& prompt) -> std::string {
            Provider step_provider;
            try {
                step_provider = options.provider_for_step ? options.provider_for_step(step) : provider;
                if (!step_provider) throw std::runtime_error("Missing step provider");
            } catch (...) {
                stopped = true;
                emit("step_failed", {{"step", step.name}, {"attempt", 0}, {"error", "provider_configuration"}});
                throw ExecutionError("Step '" + step.name + "' provider configuration failed.");
            }
            int attempt = 0, transient_retries = 0, repairs = 0;
            std::string current_prompt = prompt;
            while (true) {
                ++attempt;
                {
                    std::lock_guard<std::mutex> guard(call_mutex);
                    if (stopped.load()) throw ExecutionError("Workflow stopped after a sibling failure.");
                    if (calls >= options.max_calls) {
                        stopped = true;
                        emit("step_failed", {{"step", step.name}, {"attempt", attempt}, {"error", "call_limit"}});
                        throw ExecutionError("Provider call limit reached.");
                    }
                    ++calls;
                }
                emit("step_started", {{"step", step.name}, {"attempt", attempt}});
                try {
                    auto response = step_provider(workflow.agents.at(step.agent).role, current_prompt);
                    if (!has_text(response)) throw std::runtime_error("Empty provider result");
                    const auto failures = validation_failures(workflow, step.name, response);
                    if (!failures.empty()) {
                        const auto limit = repair_limits.count(step.name) ? repair_limits.at(step.name) : 0;
                        if (repairs < limit) {
                            ++repairs;
                            emit("step_repair", {{"step", step.name}, {"attempt", attempt},
                                                 {"repair", repairs}, {"max_repairs", limit},
                                                 {"failed_checks", failures.size()}});
                            current_prompt = prompt + "\n\nVORA DETERMINISTIC VALIDATION FAILED:\n";
                            for (const auto& failure : failures) current_prompt += "- " + failure + "\n";
                            current_prompt += "Return a corrected complete output only.";
                            continue;
                        }
                        const bool critique = std::any_of(
                            workflow.validations.begin(), workflow.validations.end(),
                            [&](const Validation& rule) {
                                return rule.step == step.name && rule.kind == "critique";
                            });
                        throw ValidationError(critique ? "critique" : "deterministic");
                    }
                    emit("step_completed", {{"step", step.name}, {"attempt", attempt}});
                    return response;
                } catch (const ValidationError& error) {
                    stopped = true;
                    emit("step_failed", {{"step", step.name}, {"attempt", attempt}, {"error", "validation_failed"}});
                    const std::string kind = std::string(error.what()) == "critique"
                        ? "critique validation" : "deterministic validation";
                    throw ExecutionError("Step '" + step.name + "' failed " + kind + ". Dependent steps did not run.");
                } catch (const TransientError&) {
                    if (transient_retries < options.retries && !stopped.load()) {
                        ++transient_retries;
                        emit("step_retry", {{"step", step.name}, {"attempt", attempt}, {"next_attempt", attempt + 1}});
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;
                    }
                    stopped = true;
                    emit("step_failed", {{"step", step.name}, {"attempt", attempt}, {"error", "transient_exhausted"}});
                    throw ExecutionError("Step '" + step.name + "' exhausted transient retries. Check the local model server.");
                } catch (...) {
                    stopped = true;
                    emit("step_failed", {{"step", step.name}, {"attempt", attempt}, {"error", "provider_failure"}});
                    throw ExecutionError("Step '" + step.name + "' failed: provider rejected the request or returned invalid output.");
                }
            }
        };
        while (!pending.empty()) {
            std::vector<std::pair<std::string, std::future<std::string>>> running;
            auto values = outputs;
            values[workflow.input_name] = input;
            for (const auto& step : workflow.steps) {
                if (running.size() >= static_cast<size_t>(options.workers)) break;
                if (!pending.count(step.name)) continue;
                bool ready = true;
                for (const auto& dep : step.dependencies) if (!outputs.count(dep)) ready = false;
                if (!ready) continue;
                const auto prompt = interpolate(step.prompt, values);
                pending.erase(step.name);
                running.emplace_back(step.name, std::async(std::launch::async, execute, step, prompt));
            }
            if (running.empty()) throw ExecutionError("Workflow cannot make progress.");
            std::exception_ptr failure;
            for (auto& task : running) {
                try { outputs[task.first] = task.second.get(); }
                catch (...) { if (!failure) failure = std::current_exception(); stopped = true; }
            }
            if (failure) std::rethrow_exception(failure);
        }
        emit("workflow_completed", {{"calls", calls}});
        return {outputs, outputs.at(workflow.output), events};
    } catch (ExecutionError& error) {
        emit("workflow_failed", {{"calls", calls}});
        error.events = events;
        throw;
    } catch (...) {
        emit("workflow_failed", {{"calls", calls}});
        ExecutionError error("Workflow execution failed.");
        error.events = events;
        throw error;
    }
}
}
