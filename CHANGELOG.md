# Changelog

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
