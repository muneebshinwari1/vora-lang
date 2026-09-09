#include "vora_parser.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void rejects(const std::string& source) {
    try {
        (void)vora::parse(source);
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("Line ") == 0,
                "Parse failure must identify its source line");
        return;
    }
    throw std::runtime_error("Invalid source was accepted: " + source);
}
} // namespace

int main() {
    try {
        const auto workflow = vora::parse(R"VORA(
# Header comments and forward references are supported.
workflow Example(company):
    agent a = "research role"
    last = a("{first} {second} {first}")
    first = a("{company}")
    second = a("independent")
    return last
# Trailing comments are permitted.
)VORA");
        require(workflow.name == "Example" && workflow.input_name == "company", "Header model");
        require(workflow.agents.at("a").role == "research role", "Agent role");
        require(workflow.output == "last", "Declared output");
        require(workflow.steps[0].dependencies == std::vector<std::string>({"first", "second"}),
                "References preserve first occurrence and remove duplicates");
        require(workflow.steps[1].dependencies.empty(), "Input is not a step dependency");
        const auto graph = vora::plan(workflow);
        require(graph.at("parallel_layers") == nlohmann::json({{"first", "second"}, {"last"}}),
                "Plan discovers independent layers in source order");
        require(graph.at("minimum_calls") == 3, "Plan counts calls");
        require(graph.at("steps").at(0).at("name") == "last", "Plan keeps source order");
        require(workflow.validations.empty() && !graph.contains("validations"),
                "Existing workflows retain their model and plan without validations");
        const vora::Workflow old_aggregate{"Old", "input", {}, {}, "output"};
        require(old_aggregate.validations.empty(), "Old aggregate initializers remain compatible");

        const auto validated = vora::parse(R"VORA(workflow Checked(input):
agent a = "role"
validate review as critique
draft = a("{input}")
review = a("Check {draft}")
validate final as critique
final = a("Check again {review}")
return final)VORA");
        require(validated.validations.size() == 2, "Validation directives are retained");
        require(validated.validations[0].step == "review" && validated.validations[0].kind == "critique",
                "Forward validation target and kind");
        require(validated.validations[1].step == "final", "Validation declaration order");
        require(vora::plan(validated).at("validations") == nlohmann::json::array({
                    {{"step", "review"}, {"kind", "critique"}},
                    {{"step", "final"}, {"kind", "critique"}}}),
                "Plan exposes structural validation directives");
        require(vora::plan(validated).at("minimum_calls") == 3,
                "Structural validation does not add model calls");

        const auto strings = vora::parse(R"VORA(workflow Strings(input):
x = a("JSON: {\"a\": 1}; {input}; line\nnext; \u0041; # literal")
agent a = "role"
y = a("{x} and {x}")
return y)VORA");
        require(strings.steps[0].prompt == "JSON: {\"a\": 1}; {input}; line\nnext; A; # literal",
                "JSON escapes, Unicode escapes, literal braces and hashes decode correctly");
        require(strings.steps[1].dependencies == std::vector<std::string>({"x"}), "Duplicate placeholder");
        const auto crlf = vora::parse("\r\nworkflow W(i):\r\nagent a = \"r\"\r\nx = a(\"{i}\")\r\nreturn x\r\n");
        require(crlf.name == "W", "CRLF handling");

        const std::string prefix = "workflow Example(input):\nagent a = \"role\"\n";
        const std::vector<std::string> invalid_bodies{
            "x = a(\"{missing}\")\nreturn x",
            "x = absent(\"hi\")\nreturn x",
            "x = a(\"{y}\")\ny = a(\"{x}\")\nreturn x",
            "x = a(\"{x}\")\nreturn x",
            "x = a(\"hi\")\nx = a(\"hi\")\nreturn x",
            "input = a(\"hi\")\nreturn input",
            "agent input = \"r\"\nx = a(\"hi\")\nreturn x",
            "agent a = \"again\"\nx = a(\"hi\")\nreturn x",
            "a = a(\"hi\")\nreturn a",
            "x = a(\"hi\")\nreturn missing",
            "x = a(\"hi\")\nreturn a",
            "x = a(\"hi\")\nreturn x\ny = a(\"later\")",
            "x = a(\"hi\")\nreturn x\nreturn x",
            "import os\nreturn x",
            "x = a(__import__(\"os\"))\nreturn x",
            "x = a(42)\nreturn x",
            "x = a(null)\nreturn x",
            "x = a(\"\")\nreturn x",
            "x = a(\"   \")\nreturn x",
            "x = a('single quotes')\nreturn x",
            "x = a(\"hi\", \"extra\")\nreturn x",
            "x = a(\"hi\") # inline comments unsupported\nreturn x",
            "x = a(\"{a}\")\nreturn x",
            "x = a(\"hi\")",
            "return input",
            "agent unused = 123\nx = a(\"hi\")\nreturn x",
            "x = a(\"hi\")\nvalidate x as truth\nreturn x",
            "x = a(\"hi\")\nvalidate missing as critique\nreturn x",
            "x = a(\"hi\")\nvalidate input as critique\nreturn x",
            "x = a(\"hi\")\nvalidate a as critique\nreturn x",
            "x = a(\"hi\")\nvalidate x as critique\nvalidate x as critique\nreturn x",
            "x = a(\"hi\")\nreturn x\nvalidate x as critique",
            "x = a(\"hi\")\nvalidate x critique\nreturn x",
            "x = a(\"hi\")\nvalidate x as\nreturn x",
        };
        for (const auto& body : invalid_bodies) {
            rejects(prefix + body);
        }
        rejects("");
        rejects("# only comments\n \t");
        rejects("workflow Invalid(input)\n");
        rejects("workflow 123(input):\n");
        rejects("workflow Bad(two, inputs):\n");
        rejects("workflow Good(i):\nreturn i");

        auto corrupt = workflow;
        corrupt.steps[1].dependencies.push_back("missing");
        bool rejected_plan = false;
        try { (void)vora::plan(corrupt); }
        catch (const std::runtime_error&) { rejected_plan = true; }
        require(rejected_plan, "Planner must terminate on invalid dependencies");
        std::cout << "PASS: native parser/model/plan, JSON strings, forward references, DAG layers, "
                  << invalid_bodies.size() + 6 << " invalid programs\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
