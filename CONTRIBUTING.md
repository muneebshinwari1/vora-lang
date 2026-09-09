# Contributing to Vora

Vora is an experimental agent-workflow language with a native C++ engine and an
older Python reference prototype. Small, focused pull requests are welcome.

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
