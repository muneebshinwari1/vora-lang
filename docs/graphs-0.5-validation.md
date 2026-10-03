# Local 0.5 graph milestone verification — 2026-10-03

Status: working Windows development milestone, not a public release. Changes
remain on the local `milestone-0.5-graphs` branch. Public release/manifests stay
on stable 0.4.0. No GitHub push or remote metadata update belongs to this phase.

## Implemented scope

- Independent parallel graph branches and dependency joins retain the existing
  bounded batch scheduler. Two reviewed branches are tested for actual overlap.
- Conditional agent calls use validated keep/revise verdicts. An unselected call
  returns the exact fallback, consumes no call and creates no phantom agent memory.
- Writer–critic loops accept only structurally valid keep verdicts, fail on
  exhausted 1..10 round limits and charge all calls/retries/repairs to one budget.
- Completed writer/reviewer stages are saved separately from ordinary outputs.
  Resume preserves call reservations, accepted stages and exhausted rounds.
- Plans expose conditions/review limits; timed traces add branch/round/verdict events.

Human approval states and an interactive trace viewer are later milestones.

## Reproducible local checks

Windows x64, Visual Studio 2022/MSVC 19.42, C++17 Release. The build and state
folders are outside OneDrive, under `%LOCALAPPDATA%/Vora/build-0.5`.

```powershell
cmake -S native -B "$env:LOCALAPPDATA/Vora/build-0.5" -A x64 -DVORA_RUNTIME_OUTPUT_DIRECTORY="$env:LOCALAPPDATA/Vora/build-0.5/bin"
cmake --build "$env:LOCALAPPDATA/Vora/build-0.5" --config Release --parallel 2
ctest --test-dir "$env:LOCALAPPDATA/Vora/build-0.5" -C Release --output-on-failure
python -m unittest discover -s tests -v
& native/Run-Graph-Demo.ps1 -Executable "$env:LOCALAPPDATA/Vora/build-0.5/bin/vora.exe"
```

- Final native CTest: **11/11 groups passed**, 26.83 seconds. Includes parser,
  runtime, validation, state, tools, allocation bounds, native graph tests and
  HTTP/state/tool/graph CLI fixtures against the exact development executable.
- After adding a dedicated process-kill graph recovery fixture, the graph HTTP
  suite passed **7/7 tests**, 6.293 seconds. This test kills only its own process,
  confirms it exited, removes only its owned empty temporary lock, and resumes
  the saved writer stage. The unfinished critic reservation stays consumed.
- Historical Python prototype: **40/40 tests passed**. It has not gained the
  native graph syntax and is not required to run the C++ engine.
- Windows CPack ZIP generated and clean installation passed: CLI/version,
  packaged graph examples/guide, parallel demo, tool grants and completed
  checkpoint roundtrip. The separate installed Scoop engine still reports 0.4.0.

Development executable SHA256:
`905856956e00e7e813036bb9b49a5f2ce0bb9053f8fff7160df5f5844f535111`.

Local archive `vora-0.5.0-dev.1-win64.zip` SHA256:
`08c990b549c04f036bc4b363af9eca2f15db92963b89de79acdb5ae6dc92c310`.

## Real-model reference runs

The existing loopback llama.cpp server ran Qwen3-0.6B-Q8_0, model alias `local`.
Five recorded jobs (two bounded reviews, two conditional edits, one parallel
research/join) reserved **11 calls, zero job failures**. All five completed
checkpoints resumed with **zero additional provider calls**. Per-job wall time
was 1.479..5.917 seconds. Inputs were simple templated notice tasks; this is a
smoke run, not a throughput claim or sustained-load qualification.

Raw private records are retained in local application-data storage. The sanitized
developer summary is `native/outputs/graphs-0.5-live-summary.json` (gitignored).
The earlier broad reviewer prompt produced invalid critique JSON and was rejected;
its checkpoint/trace are retained separately. The example prompt was made explicit
about empty keep issues. Validation was not weakened. Model correctness and the
quality of its judgements were not independently graded.

Fixtures also exercise revise-then-keep, both conditional paths, critic transient
retries, malformed critique, global call exhaustion, round exhaustion, completed
stage restoration, changed identity, malformed/missing stage state, oversized
review requests and parallel checkpoint consistency.

## Before public promotion

Linux/macOS builds and sanitizers for this new source have not run locally; public
0.4 CI results do not certify 0.5. Independent review, exact-candidate cross-platform
CI, longer real-model deployment testing and fresh non-Windows package verification
remain promotion gates. Source fixtures/workflow expectations are prepared locally.
The current stable container release guard is scoped to 0.4 tags; explicitly update
it during an authorized 0.5 promotion. Update distribution URLs/hashes only after
immutable candidate archives are published and verified. No publication is scheduled.
