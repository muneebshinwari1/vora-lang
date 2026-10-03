# Vora roadmap

Vora is an experimental native agent-workflow language. These priorities are not delivery dates or compatibility guarantees.

## Implemented on the 0.4 development line

- Windows, Linux and macOS local HTTP providers.
- CMake installation and engine packages.
- Bounded agent memory, checkpoint/resume and controlled tool steps.

## Next priorities

1. Make fresh-clone local-model setup easier, with explicit downloads and hash verification.
2. Improve diagnostics and examples for first-time authors and contributors.
3. Evaluate model quality and authoring effort against reproducible baselines.
4. Define stable language/state compatibility before a stable 0.4 release.

## Contribution ideas

- Reproduce a fresh Linux/macOS build and improve the instructions.
- Add an example combining JSON selection with deterministic validation.
- Improve an error message with a focused regression case.
- Report confusing quickstart steps with OS, compiler and exact command.

Check [open issues](https://github.com/muneebshinwari1/vora-lang/issues) before starting. Discuss grammar and capability changes first; see [CONTRIBUTING.md](CONTRIBUTING.md).
