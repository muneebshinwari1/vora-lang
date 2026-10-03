# Preview release procedure

The runtime identifies itself as `0.4.0-dev`; previews are not stable releases. Use tags such as `v0.4.0-dev.1`, leaving historical tags unchanged.

1. Update the changelog and confirm docs match the candidate commit.
2. Push the candidate and wait for all CI jobs (Windows, Linux, macOS and Python).
3. Download each platform package from that exact successful CI run. Record the commit, runner OS/architecture and SHA256 digests in release notes.
4. Create the preview tag at the tested commit and publish a GitHub prerelease.
5. Attach the unchanged CI archives and a `SHA256SUMS.txt` file.
6. State that models are excluded, libcurl is a runtime dependency on Unix, and previews do not promise stable checkpoint compatibility or AI accuracy.

Do not promote failed runs, relabel development binaries as stable, overwrite tags or silently replace assets. Fixes use a new preview tag. CI artifacts expire after 30 days; release assets are longer-lived downloads. Maintainers publish with repository write access; CI has read-only repository permissions.
