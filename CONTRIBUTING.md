# Contributing to Vora

Vora is an experimental agent-workflow language with a native C++ engine and an
older Python reference prototype. Small, focused pull requests are welcome.

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Good ways to contribute

- Reproduce and fix a parser or runtime bug.
- Add a focused validator with clear, deterministic semantics.
- Improve Windows build, local-model setup, examples, or documentation.
- Add meaningful tests for concurrency, failure handling, and output contracts.
- Improve evaluation methodology without deleting or hiding failed results.

For a first contribution, look for issues labeled `good first issue` or
`help wanted`. If none fits, open a feature request describing the concrete
workflow that is difficult to express today.

## Development workflow

1. Fork the repository and create a branch from `main`.
2. Keep one pull request focused on one problem.
3. Add tests that fail before the change and pass after it when practical.
4. Run the relevant checks below.
5. Open a pull request using the repository template and respond to review.

For grammar or runtime changes, open an issue first. State the proposed syntax,
execution semantics, failure behavior, compatibility impact, and an example.

## Before opening a pull request

1. Describe the behavior change and its limits.
2. Add or update a meaningful regression test when behavior changes.
3. Run the checks for the component you changed.
4. Do not commit model weights, generated binaries, execution outputs, logs,
   credentials, or machine-specific state.

### Native engine

On Windows with Visual Studio 2022 C++ Build Tools and CMake:

```powershell
./native/build.ps1
python ./native/tests/test_http_cli.py
```

Python is used by the HTTP fixture tests only; the native executable and local
launcher do not require Python at runtime.

### Python prototype

```powershell
python -m unittest discover -s tests -v
```

## Claims and evidence

Keep engineering, model-quality, and product claims separate. A successful
workflow execution proves data flow, not factual correctness. Preserve failed
evaluation runs and document model, prompt, fixture, and executable hashes when
reporting comparisons.

## Review and licensing

Maintainers review contributions for scope, correctness, compatibility,
security, tests, and documentation. CI must pass before merge. Contributions
are accepted under the repository's [MIT License](LICENSE). Do not submit code
you do not have the right to license.
