# Vora — a native C++ language for AI agent workflows

Write compact declarative workflows for local AI agent orchestration. Vora validates the dependency graph and runs agents and explicitly granted tools with a C++17 engine.

[![CI](https://github.com/muneebshinwari1/vora-lang/actions/workflows/ci.yml/badge.svg)](https://github.com/muneebshinwari1/vora-lang/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Contributions welcome](https://img.shields.io/badge/contributions-welcome-brightgreen.svg)](CONTRIBUTING.md)

**Status: experimental 0.4.0-dev.** Windows, Linux and macOS are covered by CI. The native runtime needs no Python interpreter. Python is development test tooling and powers the [older prototype](PYTHON_PROTOTYPE.md).

Production hardening is tracked against an explicit [deployment contract and release gates](docs/production-readiness.md). Passing CI alone does not promote a preview to a stable production release.

## An agent workflow in eight lines

```text
workflow WriteAndReview(input):
    agent writer = "Write a useful draft in at most 3 short sentences."
    agent critic = "Review the draft. Give one concrete improvement."
    agent editor = "Apply the critique. Return only the improved text."
    draft = writer("Task: {input}")
    review = critic("Task: {input}\nDraft: {draft}")
    final = editor("Task: {input}\nDraft: {draft}\nCritique: {review}")
    return final
```

Placeholders declare dependencies. Independent steps can run concurrently. Invalid references, duplicate names and cycles fail before model execution. This is a compact example, not a measured productivity comparison.

## Try it without a model

For ready-to-install packages, see the [installation guide](docs/installation.md):
Windows Scoop, macOS/Linux Homebrew, checksum-verified direct archives and
multi-platform Docker images on GitHub Packages.

```sh
docker run --rm --network none ghcr.io/muneebshinwari1/vora-lang:dev run /opt/vora/share/vora/examples/quickstart-fast.vora --provider demo --input "Hello developers"
```

To build from source:

```sh
git clone https://github.com/muneebshinwari1/vora-lang.git
cd vora-lang
```

Windows requires CMake 3.20+ and Visual Studio 2022 C++ Build Tools:

```powershell
./native/build.ps1
./native/dist/vora.exe check native/quickstart-fast.vora
./native/dist/vora.exe run native/quickstart-fast.vora --provider demo --input "Welcome to the coding club"
./native/dist/vora.exe run native/examples/file-stats.vora --input README.md --workspace . --allow-tools read_file,text_stats
```

Linux/macOS require CMake 3.20+, a C++17 compiler and libcurl development files. On Ubuntu install `build-essential cmake libcurl4-openssl-dev`; on macOS install Xcode command-line tools and CMake (system libcurl is used).

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --parallel 2
ctest --test-dir native/build --output-on-failure
./native/dist/vora run native/quickstart-fast.vora --provider demo --input "Welcome to the coding club"
./native/dist/vora run native/examples/file-stats.vora --input README.md --workspace . --allow-tools read_file,text_stats
```

`--provider demo` simulates agent responses; it does not demonstrate AI inference. File-stats reads a real file and computes statistics without a model.

## Run real local AI

Use a local llama.cpp or compatible Ollama server. The default endpoint is `http://127.0.0.1:18080/v1/chat/completions`, model alias `local`. Only loopback endpoints are accepted. See [CLI/provider instructions](native/README.md) and [pinned Windows model setup](native/docs/model-setup.md).

Source clones and engine packages contain no model weights or llama.cpp server. After building and separately installing the pinned Windows model artifacts, [`native/Run Vora.cmd`](native/Run%20Vora.cmd) launches writer → critic → editor. Small local models can produce incorrect results; read the [recorded quality comparison](native/docs/quality-comparison.md).

## Runtime features

- Declarative agent/tool steps, dependency planning and bounded concurrency.
- Structured critique checks, deterministic output rules and bounded repair.
- Shared call limits, transient transport retries and metadata traces.
- Opt-in bounded agent memory and resumable checkpoints.
- Five explicitly granted builtins: `read_file`, `list_files`, `search_file`, `text_stats` and `json_select`.
- CMake installation and ZIP/TGZ engine packages.

Tools have fixed schemas and explicit workspace grants; they are not an OS sandbox. Memory/checkpoints contain plaintext data and are trusted local files. Interrupted unfinished calls may repeat on resume. There are no shell, write/delete or web-fetch tools. See [tool contracts](native/docs/tools.md) and [persistence details](native/README.md#persistent-memory-and-resume-040-dev).

## Documentation and downloads

- [Native engine guide](native/README.md) and [language specification](SPEC.md).
- [Runnable examples](native/examples), [changelog](CHANGELOG.md) and [roadmap](ROADMAP.md).
- [Contributor guide](CONTRIBUTING.md), [security policy](SECURITY.md) and [release procedure](docs/releases.md).
- [Releases](https://github.com/muneebshinwari1/vora-lang/releases): previews are marked prerelease.
- [CI builds](https://github.com/muneebshinwari1/vora-lang/actions/workflows/ci.yml): successful runs upload platform packages retained for 30 days. GitHub sign-in may be required to download artifacts.

Packages include the engine, examples and docs. Linux/macOS packages require a compatible system libcurl; they are not universal standalone binaries.

## Help build Vora

Bug reports, examples, docs, tests and focused pull requests are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md), the [Code of Conduct](CODE_OF_CONDUCT.md) and [governance](GOVERNANCE.md). Use [issues](https://github.com/muneebshinwari1/vora-lang/issues) to report a reproducible problem or discuss a language proposal.

If Vora is useful to you, a GitHub star helps others discover it. Sharing a real workflow or contributing an example helps the project improve. Vora is open source under the [MIT License](LICENSE).
