# Native release procedure

## Historical development previews

Historical development previews identify themselves as `0.4.0-dev`. Use preview tags such as `v0.4.0-dev.1`; leave historical tags unchanged. Native `0.4.0` promotion is described below.

1. Update the changelog and confirm docs match the candidate commit.
2. Push the candidate and wait for all CI jobs (Windows, Linux, macOS and Python).
3. Download each platform package from that exact successful CI run. Record the commit, runner OS/architecture and SHA256 digests in release notes.
4. Create the preview tag at the tested commit and publish a GitHub prerelease.
5. Attach the unchanged CI archives and a `SHA256SUMS.txt` file.
6. State that models are excluded, libcurl is a runtime dependency on Unix, and previews do not promise stable checkpoint compatibility or AI accuracy.

Do not promote failed runs, relabel development binaries as stable, overwrite tags or silently replace assets. Fixes use a new preview tag. CI artifacts expire after 30 days; release assets are longer-lived downloads. Maintainers publish with repository write access; CI has read-only repository permissions.

## Native 0.4.0 promotion

0.4.0 is the first candidate governed by [the compatibility contract](compatibility.md).
Promotion requires exact-commit CI, a recorded real-model sustained-load run,
clean installation from every generated archive and native hardening evidence.
Use the tested immutable archives and SHA256SUMS.txt without rebuilding them.
Tag v0.4.0 at that exact commit; patch fixes receive a new tag/version.
Published support is limited to the trusted local deployment contract.
Previews, moving container tags and the Python prototype are separate channels.
