# Install Vora

Vora is an experimental 0.4 development preview. These channels install the
native engine; model weights and model servers are separate. No paid API key is
needed. The older Python prototype is not an install dependency.

## Windows x64: Scoop

With [Scoop](https://scoop.sh) installed:

```powershell
scoop bucket add vora https://github.com/muneebshinwari1/vora-lang
scoop install vora/vora
vora --help
vora run "$(scoop prefix vora)/share/vora/examples/quickstart-fast.vora" --provider demo --input "Hello developers"
```

The manifest pins the published preview archive and SHA256. Scoop manages PATH,
upgrades and uninstall: `scoop update vora` and `scoop uninstall vora`. New release
versions become available when maintainers update the manifest. This is the
project's custom bucket, not a listing in Scoop's official main bucket.

## macOS and Linux: Homebrew

With [Homebrew](https://brew.sh) installed:

```sh
brew tap muneebshinwari1/vora https://github.com/muneebshinwari1/vora-lang
brew install muneebshinwari1/vora/vora
brew test muneebshinwari1/vora/vora
vora --help
vora run "$(brew --prefix vora)/share/vora/examples/quickstart-fast.vora" --provider demo --input "Hello developers"
```

If your Homebrew version asks for trust, review `Formula/vora.rb` and run
`brew trust --formula muneebshinwari1/vora/vora` before installation.
The formula builds checksum-pinned source, installs CMake/libcurl dependencies
and runs native tests. It supports the architectures supported by your compiler
and Homebrew. This is a custom tap, not homebrew/core. Use `brew upgrade vora`
after a manifest update or `brew uninstall vora` to remove it.

## Direct Linux x64 / macOS ARM64 installer

From a checked-out repository, review and run:

```sh
sh scripts/install.sh
export PATH="$HOME/.local/share/vora/0.4.0-dev.1/bin:$PATH"
vora --help
```

It verifies an embedded SHA256 before extracting, checks the installed engine,
and refuses existing destinations. Set `VORA_INSTALL_DIR` to an empty custom
directory. It does not change your shell profile or require sudo. Linux archives
target the GitHub Ubuntu runner and need compatible glibc/libcurl; use Homebrew
or build from source for other distributions. macOS binaries are unsigned and
not notarized. Do not bypass OS warnings for an unverified download.
To uninstall, remove only the installation directory you chose and its PATH
entry; keep your workflow/state files. No user state is stored in that folder
unless you explicitly choose to put it there.

## Docker / GitHub Packages

Development images are published only after all main-branch CI jobs pass.
`dev` follows the latest tested development commit; pin the `sha-COMMIT` tag or
image digest for reproducible runs. The images support Linux amd64 and arm64.
Docker Desktop runs these Linux images on Windows/macOS.

```sh
docker pull ghcr.io/muneebshinwari1/vora-lang:dev
docker run --rm --network none ghcr.io/muneebshinwari1/vora-lang:dev --help
docker run --rm --network none ghcr.io/muneebshinwari1/vora-lang:dev run /opt/vora/share/vora/examples/quickstart-fast.vora --provider demo --input "Hello developers"
docker run --rm --network none -v "$PWD:/workspace:ro" ghcr.io/muneebshinwari1/vora-lang:dev run /opt/vora/share/vora/examples/file-stats.vora --input README.md --workspace /workspace --allow-tools read_file,text_stats
```

In PowerShell use `${PWD}` for the mounted current directory. Containers run as
UID/GID 10001, contain no models and do not expose a server port. Workspace mounts
are read-only in this example. To save results/checkpoints, explicitly mount a
separate writable output directory with permissions for that UID; on Linux you
can use `--user "$(id -u):$(id -g)"` instead. Keep state outside disposable containers.

### Real model networking

Vora retains its loopback-only endpoint policy inside containers. Container
`127.0.0.1` normally refers to the container, not your host. With a trusted local
model server on port 18080, use `--network host` on Linux, or enable host
networking on Docker Desktop 4.34+ before using that option:

```sh
docker run --rm --network host ghcr.io/muneebshinwari1/vora-lang:dev run /opt/vora/share/vora/examples/quickstart-fast.vora --input "Write a welcome message"
```

Alternatively share a local model container's network namespace using
`--network container:MODEL_CONTAINER`. Host networking broadens network access;
it is unnecessary for demo or tool-only runs. `host.docker.internal` is not an
accepted model endpoint under the current loopback policy. See
[Docker host networking](https://docs.docker.com/engine/network/drivers/host/).

## Build and package from source

See [README](../README.md) and [CONTRIBUTING](../CONTRIBUTING.md). CMake installs
examples/docs alongside the executable; CPack creates ZIP/TGZ archives. Downloads
and their checksums are also available in [Releases](https://github.com/muneebshinwari1/vora-lang/releases).

## Maintainer checklist

For each preview, update the Scoop manifest, Homebrew source URL/hash and direct
installer version/hashes together. Run package-manager and installer CI checks.
Publish containers only from a commit whose full CI passed; preserve immutable
SHA tags. Check that the GHCR package visibility is public and verify an anonymous
pull. Never attach model weights, credentials or runtime state to packages.
Winget/community registry submissions and signed/notarized native installers
remain separate work; these installation routes do not claim those approvals.
