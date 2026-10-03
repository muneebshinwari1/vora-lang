#pragma once

// Declarative .vora grammar. Parsing never evaluates code or invokes tools.
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "vora_io.hpp"

namespace vora {

struct Agent {
    std::string name;
    std::string role;
};

struct Step {
    std::string name;
    std::string agent;
    std::string prompt;
    std::vector<std::string> dependencies;
    std::string kind = "agent";
};

// Declares response structure checks only; it does not establish factual truth.
struct Validation {
    std::string step;
    std::string kind;
    std::string value{};
    int number = 0;
};

struct Repair {
    std::string step;
    int max_attempts = 0;
};

struct Workflow {
    std::string name;
    std::string input_name;
    std::map<std::string, Agent> agents;
    std::vector<Step> steps;
    std::string output;
    std::vector<Validation> validations{};
    std::vector<Repair> repairs{};
    std::map<std::string, std::string> tools{};
};

inline const std::set<std::string>& builtin_tools() {
    static const std::set<std::string> names{"read_file", "list_files", "search_file", "text_stats", "json_select"};
    return names;
}

inline nlohmann::json tool_arguments(const std::string& tool, const std::string& encoded) {
    bool duplicate = false;
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int depth, nlohmann::json::parse_event_t event, nlohmann::json& item) {
        if (depth > 16) throw std::runtime_error("Tool arguments exceed nesting limit.");
        if (event == nlohmann::json::parse_event_t::object_start) keys.emplace_back();
        else if (event == nlohmann::json::parse_event_t::key && !keys.empty())
            if (!keys.back().insert(item.get<std::string>()).second) duplicate = true;
        if (event == nlohmann::json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
        return true;
    };
    const auto args = nlohmann::json::parse(encoded, callback, false);
    if (duplicate || !args.is_object()) throw std::runtime_error("Tool arguments must be a JSON object with unique keys.");
    std::set<std::string> required, optional;
    if (tool == "read_file") required = {"path"};
    else if (tool == "list_files") optional = {"path"};
    else if (tool == "search_file") { required = {"path", "query"}; optional = {"max_matches"}; }
    else if (tool == "text_stats") required = {"text"};
    else if (tool == "json_select") required = {"text", "pointer"};
    else throw std::runtime_error("Unknown builtin tool.");
    for (const auto& key : required) if (!args.contains(key)) throw std::runtime_error("Missing tool argument: " + key);
    for (const auto& item : args.items()) {
        if (!required.count(item.key()) && !optional.count(item.key())) throw std::runtime_error("Unknown tool argument: " + item.key());
        if (item.key() == "max_matches") {
            if (!item.value().is_number_integer() || item.value() < 1 || item.value() > 100)
                throw std::runtime_error("max_matches must be an integer 1..100.");
        } else {
            if (!item.value().is_string()) throw std::runtime_error("Tool argument must be a string.");
            const auto value = item.value().get<std::string>();
            const size_t maximum = item.key() == "text" ? 256 * 1024 : 4096;
            if (value.size() > maximum || value.find('\0') != std::string::npos)
                throw std::runtime_error("Tool argument exceeds its text limit.");
        }
    }
    return args;
}

namespace parser_detail {

inline std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    return first < last ? std::string(first, last) : std::string{};
}

[[noreturn]] inline void fail(std::size_t line, const std::string& reason) {
    throw std::runtime_error("Line " + std::to_string(line) + ": " + reason);
}

inline std::string string_value(const std::string& value, std::size_t line) {
    nlohmann::json parsed;
    try {
        auto depth_limit = [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if (depth > 64) throw std::runtime_error("Source value exceeds nesting limit.");
            return true;
        };
        parsed = nlohmann::json::parse(value, depth_limit);
    } catch (const nlohmann::json::exception&) {
        fail(line, "use a double-quoted JSON string");
    } catch (const std::runtime_error&) {
        fail(line, "source value exceeds nesting limit");
    }
    if (!parsed.is_string()) {
        fail(line, "expected a non-empty string");
    }
    auto result = parsed.get<std::string>();
    if (trim(result).empty()) {
        fail(line, "expected a non-empty string");
    }
    return result;
}

inline bool dependencies_ready(const Step& step, const std::set<std::string>& visited) {
    return std::all_of(step.dependencies.begin(), step.dependencies.end(),
                       [&](const std::string& name) { return visited.count(name) != 0; });
}

} // namespace parser_detail

inline Workflow parse(const std::string& source) {
    using parser_detail::fail;
    if (source.size() > 1024 * 1024) fail(1, "workflow exceeds 1 MiB");
    static const std::regex header_pattern(
        R"(^workflow ([A-Za-z_][A-Za-z0-9_]*)\(([A-Za-z_][A-Za-z0-9_]*)\):$)");
    static const std::regex agent_pattern(R"(^agent ([A-Za-z_][A-Za-z0-9_]*) = (.+)$)");
    static const std::regex tool_pattern(R"(^tool ([A-Za-z_][A-Za-z0-9_]*) = (.+)$)");
    static const std::regex step_pattern(
        R"(^([A-Za-z_][A-Za-z0-9_]*) = ([A-Za-z_][A-Za-z0-9_]*)\((.+)\)$)");
    static const std::regex return_pattern(R"(^return ([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex validation_pattern(
        R"(^validate ([A-Za-z_][A-Za-z0-9_]*) as ([A-Za-z_][A-Za-z0-9_]*)$)");
    static const std::regex text_rule_pattern(
        R"(^(require|forbid) ([A-Za-z_][A-Za-z0-9_]*) contains (.+)$)");
    static const std::regex sentence_rule_pattern(
        R"(^require ([A-Za-z_][A-Za-z0-9_]*) sentences ([0-9]+)$)");
    static const std::regex repair_pattern(
        R"(^repair ([A-Za-z_][A-Za-z0-9_]*) max ([0-9]+)$)");
    static const std::regex placeholder_pattern(R"(\{([A-Za-z_][A-Za-z0-9_]*)\})");

    std::vector<std::pair<std::size_t, std::string>> lines;
    std::istringstream input(source);
    std::string raw_line;
    std::size_t line_number = 0;
    while (std::getline(input, raw_line)) {
        ++line_number;
        if (raw_line.size() > 8192) fail(line_number, "source line exceeds 8192 bytes");
        auto line = parser_detail::trim(raw_line);
        if (!line.empty() && line.front() != '#') {
            lines.emplace_back(line_number, std::move(line));
            if (lines.size() > 1024) fail(line_number, "workflow exceeds 1024 statements");
        }
    }
    if (lines.empty()) {
        fail(1, "empty workflow");
    }
    std::smatch match;
    if (!std::regex_match(lines.front().second, match, header_pattern)) {
        fail(lines.front().first, "expected workflow Name(input):");
    }
    Workflow workflow;
    workflow.name = match[1].str();
    workflow.input_name = match[2].str();
    std::set<std::string> symbols{workflow.input_name};
    std::map<std::string, std::size_t> step_lines;
    std::map<std::string, std::size_t> validation_lines;
    std::map<std::string, std::size_t> repair_lines;
    std::size_t return_line = lines.front().first;
    bool returned = false;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto& [number, line] = lines[i];
        if (returned) {
            fail(number, "return must be the last statement");
        }
        if (std::regex_match(line, match, agent_pattern)) {
            const auto symbol = match[1].str();
            if (!symbols.insert(symbol).second) {
                fail(number, "duplicate name '" + symbol + "'");
            }
            workflow.agents.emplace(symbol, Agent{symbol, parser_detail::string_value(match[2].str(), number)});
        } else if (std::regex_match(line, match, tool_pattern)) {
            const auto symbol = match[1].str();
            if (!symbols.insert(symbol).second) fail(number, "duplicate name '" + symbol + "'");
            const auto builtin = parser_detail::string_value(match[2].str(), number);
            if (!builtin_tools().count(builtin)) fail(number, "unknown builtin tool '" + builtin + "'");
            workflow.tools.emplace(symbol, builtin);
        } else if (std::regex_match(line, match, step_pattern)) {
            const auto symbol = match[1].str();
            if (!symbols.insert(symbol).second) {
                fail(number, "duplicate name '" + symbol + "'");
            }
            const auto argument = parser_detail::trim(match[3].str());
            const bool object_argument = !argument.empty() && argument.front() == '{';
            workflow.steps.push_back(Step{symbol, match[2].str(),
                object_argument ? argument : parser_detail::string_value(argument, number), {},
                object_argument ? "tool" : "agent"});
            step_lines.emplace(symbol, number);
        } else if (std::regex_match(line, match, validation_pattern)) {
            const auto target = match[1].str();
            const auto kind = match[2].str();
            if (kind != "critique") {
                fail(number, "unsupported validation kind '" + kind + "'");
            }
            if (!validation_lines.emplace(target, number).second) {
                fail(number, "duplicate validation for step '" + target + "'");
            }
            workflow.validations.push_back(Validation{target, kind});
        } else if (std::regex_match(line, match, text_rule_pattern)) {
            const auto kind = match[1].str() == "require" ? "contains" : "not_contains";
            const auto target = match[2].str();
            const auto value = parser_detail::string_value(match[3].str(), number);
            workflow.validations.push_back(Validation{target, kind, value, 0});
        } else if (std::regex_match(line, match, sentence_rule_pattern)) {
            const auto target = match[1].str();
            const auto count = std::stoi(match[2].str());
            if (count < 1 || count > 100) fail(number, "sentence count must be 1..100");
            workflow.validations.push_back(Validation{target, "sentences", {}, count});
        } else if (std::regex_match(line, match, repair_pattern)) {
            const auto target = match[1].str();
            const auto attempts = std::stoi(match[2].str());
            if (attempts < 1 || attempts > 5) fail(number, "repair attempts must be 1..5");
            if (!repair_lines.emplace(target, number).second)
                fail(number, "duplicate repair for step '" + target + "'");
            workflow.repairs.push_back(Repair{target, attempts});
        } else if (std::regex_match(line, match, return_pattern)) {
            workflow.output = match[1].str();
            return_line = number;
            returned = true;
        } else {
            fail(number, "unsupported statement");
        }
    }
    if (!returned || step_lines.count(workflow.output) == 0) {
        fail(return_line, "workflow must return a defined step");
    }
    for (const auto& validation : workflow.validations) {
        if (step_lines.count(validation.step) == 0) {
            const auto location = validation_lines.count(validation.step)
                ? validation_lines.at(validation.step) : return_line;
            fail(location,
                 "undefined validation target '" + validation.step + "'");
        }
    }
    for (const auto& repair : workflow.repairs) {
        if (step_lines.count(repair.step) == 0)
            fail(repair_lines.at(repair.step), "undefined repair target '" + repair.step + "'");
        if (std::none_of(workflow.validations.begin(), workflow.validations.end(),
                         [&](const Validation& rule) { return rule.step == repair.step; }))
            fail(repair_lines.at(repair.step), "repair target must have a validation rule");
    }
    for (auto& step : workflow.steps) {
        const auto number = step_lines.at(step.name);
        if (workflow.tools.count(step.agent)) {
            step.kind = "tool";
            try { (void)tool_arguments(workflow.tools.at(step.agent), step.prompt); }
            catch (const std::exception& error) { fail(number, error.what()); }
            if (std::any_of(workflow.repairs.begin(), workflow.repairs.end(),
                [&](const Repair& repair) { return repair.step == step.name; }))
                fail(number, "tool steps cannot have automatic repair");
        } else if (step.kind == "tool") {
            fail(number, "JSON object arguments require a declared tool");
        } else if (workflow.agents.count(step.agent) == 0) {
            fail(number, "undefined agent '" + step.agent + "'");
        }
        std::set<std::string> seen;
        for (auto ref = std::sregex_iterator(step.prompt.begin(), step.prompt.end(), placeholder_pattern);
             ref != std::sregex_iterator(); ++ref) {
            const auto symbol = (*ref)[1].str();
            if (symbol == workflow.input_name || !seen.insert(symbol).second) {
                continue;
            }
            if (step_lines.count(symbol) == 0) {
                fail(number, "undefined reference '" + symbol + "'");
            }
            step.dependencies.push_back(symbol);
        }
    }
    std::set<std::string> visited;
    while (visited.size() < workflow.steps.size()) {
        std::vector<std::string> ready;
        for (const auto& step : workflow.steps) {
            if (visited.count(step.name) == 0 && parser_detail::dependencies_ready(step, visited)) {
                ready.push_back(step.name);
            }
        }
        if (ready.empty()) {
            for (const auto& step : workflow.steps) {
                if (visited.count(step.name) == 0) {
                    fail(step_lines.at(step.name), "dependency cycle involving '" + step.name + "'");
                }
            }
        }
        visited.insert(ready.begin(), ready.end());
    }
    return workflow;
}

inline Workflow load(const std::string& path) {
    return parse(read_bounded(path, 1024 * 1024));
}

inline nlohmann::json plan(const Workflow& workflow) {
    std::set<std::string> names;
    for (const auto& step : workflow.steps) {
        if (!names.insert(step.name).second) {
            throw std::runtime_error("Cannot plan duplicate step names");
        }
    }
    std::set<std::string> visited;
    nlohmann::json layers = nlohmann::json::array();
    while (visited.size() < workflow.steps.size()) {
        std::vector<std::string> ready;
        for (const auto& step : workflow.steps) {
            if (visited.count(step.name) == 0 && parser_detail::dependencies_ready(step, visited)) {
                ready.push_back(step.name);
            }
        }
        if (ready.empty()) {
            throw std::runtime_error("Dependency cycle or undefined dependency");
        }
        layers.push_back(ready);
        visited.insert(ready.begin(), ready.end());
    }
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& step : workflow.steps) {
        nlohmann::json item = {{"name", step.name}, {"agent", step.agent}, {"dependencies", step.dependencies}};
        if (step.kind == "tool") { item["kind"] = "tool"; item["builtin"] = workflow.tools.at(step.agent); }
        steps.push_back(std::move(item));
    }
    nlohmann::json result = {{"workflow", workflow.name}, {"input", workflow.input_name},
                             {"output", workflow.output}, {"minimum_calls", workflow.steps.size()},
                             {"parallel_layers", std::move(layers)}, {"steps", std::move(steps)}};
    if (!workflow.tools.empty()) result["tools"] = workflow.tools;
    if (!workflow.validations.empty()) {
        result["validations"] = nlohmann::json::array();
        for (const auto& validation : workflow.validations) {
            nlohmann::json item = {{"step", validation.step}, {"kind", validation.kind}};
            if (!validation.value.empty()) item["value"] = validation.value;
            if (validation.number) item["number"] = validation.number;
            result["validations"].push_back(std::move(item));
        }
    }
    if (!workflow.repairs.empty()) {
        result["repairs"] = nlohmann::json::array();
        for (const auto& repair : workflow.repairs)
            result["repairs"].push_back({{"step", repair.step}, {"max_attempts", repair.max_attempts}});
    }
    return result;
}

} // namespace vora
