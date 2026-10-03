# Changelog

## 0.4.0-dev — development previews

- Enforce prompt and tool-field limits before interpolation appends, including
  memory and repair context; add guarded allocation regression coverage.
- Bound workflow/input reads, source lines/statements, calls and expanded prompts.
- Reject deeply nested state/model JSON and oversized embedded-provider results.
- Atomically save result/trace JSON and reject collisions with source/input/state.
- Flush state files to storage and sync Unix parent directories before returning.
- Add configurable local HTTP attempt timeouts, bound into checkpoint identity.
- Register CLI fixtures with CTest and add an ASan/UBSan CI job.
- Add failure regressions and 200 repeated graph runs to the native test suite.
- Document production deployment scope, recovery and outstanding release gates.

- Add a multi-stage non-root Docker image and tested amd64/arm64 GHCR publication.
- Add checksum-pinned Scoop/Homebrew manifests and a direct Unix installer.
- Add installation, model networking, update and uninstall documentation.

- Add Linux/macOS libcurl HTTP support alongside Windows WinHTTP.
- Add CMake installation and ZIP/TGZ engine packages.
- Add bounded opt-in agent memory and configuration-bound checkpoint/resume.
- Add five capability-controlled builtins and tool-only workflows.
- Add native state/tools and HTTP/CLI regression coverage across three platforms.
- Align native-first documentation, contributor instructions and security scope.
- Publish tested CI package artifacts and document preview releases.


## 0.3.0 — 2026-09-09

- Add native `require STEP sentences N` validation.
- Add exact, case-sensitive `require STEP contains "text"` validation.
- Add exact, case-sensitive `forbid STEP contains "text"` validation.
- Add bounded `repair STEP max N` model repair calls.
- Record repair attempts and failed-check counts without response content in traces.
- Keep repair attempts separate from transient transport retries while sharing the
  global provider-call limit.
- Add parser/runtime regression coverage and a runnable guarded-notice example.

## 0.2.0 — 2026-09-08

- Add the native C++17 workflow engine and local llama.cpp provider.
- Add structured critique validation, call limits, retries, traces, and local
  Windows launchers.
- Add fixed model-quality evaluation fixtures and independent grading.
