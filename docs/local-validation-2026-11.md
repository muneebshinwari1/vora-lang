# Local 0.5 validation follow-up

This is local development evidence, not a published release or a claim of
production certification. Runtime source reviewed and tested is commit
`49e1f7d88f15abe3616c597c60276a923e389235` on `milestone-0.5-graphs`.
The follow-up changes affect Docker validation and the package verifier.

## Release builds and packaging

- Windows native/CLI suite at that commit: 13/13 passed (50.49 seconds),
  recorded in `native/outputs/windows-followup-20261106.log`.
- Linux x86-64, Debian bookworm, GNU C++ 12.2: 13/13 passed (19.55 seconds)
  in the release stage, recorded in `native/outputs/linux-sanitizers-20261106.log`.
- On November 8, the Linux TGZ clean-install verifier passed, including graph
  examples, approval pause/inspect/decide/resume, completed-checkpoint resume,
  explicit filesystem tool grants and packaged documentation/licenses.
  Log: `native/outputs/linux-package-fixed-20261108.log`.
- Debian's Python lacked `tarfile.extractall(filter=...)`. The verifier now
  validates every member before copying only regular files/directories into a
  fresh temporary directory. Traversal, symlink, hardlink and FIFO fixtures were
  rejected. An additional read-only agent review found no concrete regression
  for this generated-package use case.
- The tested Docker source image excluded `native/local-model`, `native/outputs`
  and `native/build`, while retaining the approval CLI test fixture. Logs and
  private model/state artifacts are not part of the image's source context.

Logs mentioned here are local, ignored artifacts, not public CI records.

The November 8 follow-up build repeated the Linux release suite: **13/13 passed
in 19.61 seconds**. Its packaged release binary passed the clean TGZ verifier
again (`native/outputs/linux-package-final-20261108.log`).

The final runtime-stage recipe was assembled using `/opt/vora` from that tested
image, without rebuilding the source. UID 10001 was asserted; CLI help, a demo
workflow and explicitly granted file-stat tools passed with `--network none`.
Logs: `native/outputs/linux-runtime-final-20261108.log` and
`native/outputs/linux-runtime-smoke-20261108.log`. An earlier full runtime build
was interrupted and is not counted as a successful build. This smoke test uses
the simulated provider, not real model inference.

## Sanitizer investigation

The first Debian sanitizer build lacked the Clang runtime archive; the Docker
stage now explicitly installs `libclang-rt-14-dev`. The next instrumented run
compiled but failed 6 of 13 CTest groups with SIGSEGV, often without diagnostics.
The failed log is retained as `native/outputs/linux-sanitizers-20261107.log`.

On the same WSL Linux host (`vm.mmap_rnd_bits=32`), a standalone
`int main() { return 0; }` compiled with Clang 14 and ASan/UBSan failed 17 of
50 executions. Adding `-fno-pie` at compile time and `-no-pie` at link time gave
50/50 successful executions. This establishes that these startup failures can
occur without Vora code; it does not by itself establish Vora memory safety.
An additional GCC comparison hung and was stopped, so it has no pass result.
Reproduction output: `native/outputs/asan-repro-20261108.log`.

The Docker sanitizer target uses the non-PIE diagnostic configuration and keeps
ASan, UBSan and leak detection enabled. Normal release compiler/linker flags and
host ASLR configuration remain unchanged. The separate `sanitizer-build` target
allows investigating failures without first requiring CTest success.

```sh
docker build --target sanitizers -t vora-local-sanitizers:0.5 .
```

Result on November 8: **13/13 ASan/UBSan CTest groups passed in 28.70 seconds**,
including all native and HTTP/state/tools/graphs/approval CLI groups, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
Log: `native/outputs/linux-sanitizers-nonpie-20261108.log`.
This result covers the documented non-PIE instrumented configuration, not the
previously failing PIE configuration or every supported operating system.

## Independent agent review

A separate read-only agent completed a scoped review of `dc42485..49e1f7d`
on November 8. It reviewed gate dependencies, snapshot binding, malformed state,
request identity, terminal decisions, locks, atomic persistence, pause/resume,
budget retention and documented trust boundaries. No additional concrete
unresolved approval bypass was found under the trusted-local-state model.

It independently executed four focused approval CLI tests (4/4, 2.868 seconds):
stale/duplicate/unknown decisions, terminal rejection, process-kill recovery after
approval, and malformed/forged state. Native graph and approval tests also passed,
including the review-round `4294967297` bounds-before-narrowing regression.
Tested Windows executable SHA256:
`fa9556a807af5f44d3b0dfde2261ac4e3bb3bc758d60d1c3789bc9f5fa80a7a2`.

This was an AI-agent code review, not professional third-party certification.
It did not independently run the full suite, Linux, macOS or sanitizers, perform
fuzzing, or audit dependencies, the model server or a network service.

## Remaining boundaries

`--by` remains a self-reported operator label. A writer with access to checkpoint
files can rewrite state; approvals do not authenticate a human or resist such
tampering. The [authenticated approval design](authenticated-approval-design.md)
is a proposal, not an implemented service.

macOS execution, deployment-specific sustained load, authenticated-service
implementation and exact-candidate public CI/release promotion remain pending.
Public packages remain unchanged while this branch is held locally.
