#include "vora_runtime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

vora::ExecutionError expect_failure(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const vora::ExecutionError& error) {
        return error;
    }
    throw std::runtime_error("Expected ExecutionError, but operation succeeded");
}

const std::string single_source = R"VORA(workflow Test(input):
agent worker = "PRIVATE_ROLE"
answer = worker("PRIVATE_PROMPT {input}")
return answer
)VORA";

const std::string chain_source = R"VORA(workflow Chain(input):
agent worker = "worker"
first = worker("first {input}")
second = worker("second {first}")
return second
)VORA";

const std::string parallel_source = R"VORA(workflow Parallel(input):
agent worker = "worker"
left = worker("left {input}")
right = worker("right {input}")
answer = worker("join {left} {right}")
return answer
)VORA";

void chain_and_nonrecursive_substitution() {
    std::vector<std::string> prompts;
    auto result = vora::run(vora::parse(chain_source), "{missing}",
        [&](const std::string&, const std::string& prompt) {
            prompts.push_back(prompt);
            return prompts.size() == 1 ? std::string("{input} {first}") : std::string("done");
        });
    require(prompts.size() == 2, "Chain did not execute exactly twice");
    require(prompts[0] == "first {missing}", "Input was recursively expanded");
    require(prompts[1] == "second {input} {first}", "Upstream result was recursively expanded");
    require(result.result == "done", "Wrong declared result");
    require(result.outputs.at("first") == "{input} {first}", "Upstream result corrupted");
}

void actual_parallel_overlap_and_join() {
    std::mutex mutex;
    std::condition_variable condition;
    int arrivals = 0;
    std::atomic<int> calls{0};
    auto result = vora::run(vora::parse(parallel_source), "x",
        [&](const std::string&, const std::string& prompt) {
            ++calls;
            if (prompt.rfind("join ", 0) == 0) {
                require(prompt == "join LEFT RIGHT", "Join received missing/wrong dependency outputs");
                return std::string("joined");
            }
            std::unique_lock<std::mutex> lock(mutex);
            ++arrivals;
            condition.notify_all();
            require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return arrivals == 2; }),
                    "Independent branches did not overlap within two seconds");
            return prompt.rfind("left ", 0) == 0 ? std::string("LEFT") : std::string("RIGHT");
        }, vora::RunOptions{2, 0, 20});
    require(result.result == "joined", "Parallel workflow failed to join");
    require(calls == 3, "Parallel workflow made an unexpected number of calls");
}

void transient_retry_recovers() {
    std::atomic<int> calls{0};
    auto result = vora::run(vora::parse(single_source), "x",
        [&](const std::string&, const std::string&) {
            if (++calls == 1) throw vora::TransientError("temporary PRIVATE_TOKEN");
            return std::string("recovered");
        }, vora::RunOptions{1, 1, 20});
    require(calls == 2, "One transient retry should make two total attempts");
    require(result.result == "recovered", "Retry lost successful output");
    require(result.events.dump().find("PRIVATE_TOKEN") == std::string::npos, "Retry leaked exception text");
}

void exhausted_retry_count() {
    std::atomic<int> calls{0};
    expect_failure([&] {
        vora::run(vora::parse(single_source), "x",
            [&](const std::string&, const std::string&) -> std::string {
                ++calls;
                throw vora::TransientError("retry");
            }, vora::RunOptions{1, 2, 20});
    });
    require(calls == 3, "Two retries must mean three total attempts");
}

void max_calls_includes_retries() {
    std::atomic<int> calls{0};
    expect_failure([&] {
        vora::run(vora::parse(single_source), "x",
            [&](const std::string&, const std::string&) -> std::string {
                ++calls;
                throw vora::TransientError("retry");
            }, vora::RunOptions{1, 10, 2});
    });
    require(calls == 2, "Retry execution exceeded or failed to consume two-call allowance");
}

void parallel_retry_call_limit() {
    std::atomic<int> calls{0};
    expect_failure([&] {
        vora::run(vora::parse(parallel_source), "x",
            [&](const std::string&, const std::string&) -> std::string {
                ++calls;
                throw vora::TransientError("retry");
            }, vora::RunOptions{2, 10, 3});
    });
    require(calls == 3, "Concurrent retries did not respect the shared three-call allowance");
}

void permanent_failure_stops_dependents_without_retry() {
    std::atomic<int> calls{0};
    auto error = expect_failure([&] {
        vora::run(vora::parse(chain_source), "x",
            [&](const std::string&, const std::string&) -> std::string {
                ++calls;
                throw std::runtime_error("PRIVATE_TOKEN");
            }, vora::RunOptions{2, 4, 20});
    });
    require(calls == 1, "Permanent failure was retried or dependent was executed");
    require(std::string(error.what()).find("PRIVATE_TOKEN") == std::string::npos, "Failure message leaked secret");
    require(error.events.dump().find("PRIVATE_TOKEN") == std::string::npos, "Failure trace leaked secret");
}

void failed_parallel_branch_blocks_join() {
    std::mutex mutex;
    std::condition_variable condition;
    int arrivals = 0;
    std::atomic<int> joins{0};
    expect_failure([&] {
        vora::run(vora::parse(parallel_source), "x",
            [&](const std::string&, const std::string& prompt) {
                if (prompt.rfind("join ", 0) == 0) {
                    ++joins;
                    return std::string("unexpected");
                }
                std::unique_lock<std::mutex> lock(mutex);
                ++arrivals;
                condition.notify_all();
                require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return arrivals == 2; }),
                        "Parallel failure test did not start both branches");
                lock.unlock();
                if (prompt.rfind("left ", 0) == 0) throw std::runtime_error("failure");
                return std::string("right done");
            }, vora::RunOptions{2, 0, 20});
    });
    require(arrivals == 2, "Both independent branches must have started for this test");
    require(joins == 0, "Join ran after one dependency failed");
}

void blank_output_stops_dependents() {
    for (const auto& output : std::vector<std::string>{"", " \n\t\r", "\f\v"}) {
        std::atomic<int> calls{0};
        expect_failure([&] {
            vora::run(vora::parse(chain_source), "x",
                [&](const std::string&, const std::string&) {
                    ++calls;
                    return output;
                }, vora::RunOptions{2, 3, 20});
        });
        require(calls == 1, "Blank output was retried or propagated to a dependent");
    }
}

void nonstandard_exception_is_sanitized() {
    std::atomic<int> calls{0};
    auto error = expect_failure([&] {
        vora::run(vora::parse(chain_source), "x",
            [&](const std::string&, const std::string&) -> std::string {
                ++calls;
                throw std::string("PRIVATE_NONSTANDARD_TOKEN");
            }, vora::RunOptions{2, 2, 20});
    });
    require(calls == 1, "Non-standard failure retried or executed dependent");
    require((std::string(error.what()) + error.events.dump()).find("PRIVATE_NONSTANDARD_TOKEN") == std::string::npos,
            "Non-standard exception content leaked");
}

void success_and_failure_logs_exclude_content() {
    auto workflow = vora::parse(single_source);
    auto result = vora::run(workflow, "PRIVATE_INPUT",
        [](const std::string&, const std::string&) { return std::string("PRIVATE_OUTPUT"); });
    auto error = expect_failure([&] {
        vora::run(workflow, "PRIVATE_INPUT",
            [](const std::string&, const std::string&) -> std::string {
                throw vora::TransientError("PRIVATE_TOKEN");
            });
    });
    const auto rendered = result.events.dump() + error.events.dump() + error.what();
    for (const auto& secret : {"PRIVATE_INPUT", "PRIVATE_OUTPUT", "PRIVATE_ROLE", "PRIVATE_PROMPT", "PRIVATE_TOKEN"}) {
        require(rendered.find(secret) == std::string::npos, std::string("Log/error leaked ") + secret);
    }
    require(result.result == "PRIVATE_OUTPUT", "Returned content should remain available to the caller");
}

void invalid_limits_make_zero_calls() {
    for (const auto options : std::vector<vora::RunOptions>{{0, 0, 20}, {-1, 0, 20}, {1, -1, 20}, {1, 0, 0}}) {
        std::atomic<int> calls{0};
        expect_failure([&] {
            vora::run(vora::parse(single_source), "x",
                [&](const std::string&, const std::string&) { ++calls; return std::string("ok"); }, options);
        });
        require(calls == 0, "Invalid controls invoked provider");
    }
}

void minimum_calls_preflight() {
    std::atomic<int> calls{0};
    expect_failure([&] {
        vora::run(vora::parse(parallel_source), "x",
            [&](const std::string&, const std::string&) { ++calls; return std::string("ok"); },
            vora::RunOptions{2, 0, 2});
    });
    require(calls == 0, "Insufficient minimum call allowance was not rejected before execution");
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"chain and nonrecursive substitution", chain_and_nonrecursive_substitution},
        {"actual parallel overlap and join", actual_parallel_overlap_and_join},
        {"transient retry recovers", transient_retry_recovers},
        {"exhausted retry count", exhausted_retry_count},
        {"max calls includes retries", max_calls_includes_retries},
        {"parallel retry call limit", parallel_retry_call_limit},
        {"permanent failure stops dependents", permanent_failure_stops_dependents_without_retry},
        {"failed parallel branch blocks join", failed_parallel_branch_blocks_join},
        {"blank output stops dependents", blank_output_stops_dependents},
        {"non-standard exception sanitized", nonstandard_exception_is_sanitized},
        {"content excluded from logs", success_and_failure_logs_exclude_content},
        {"invalid limits make zero calls", invalid_limits_make_zero_calls},
        {"minimum calls preflight", minimum_calls_preflight},
    };
    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": non-standard exception\n";
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " runtime tests passed\n";
    return failures == 0 ? 0 : 1;
}
