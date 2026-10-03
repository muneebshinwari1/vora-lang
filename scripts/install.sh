#!/bin/sh
# Install the fixed, checksum-verified preview without root privileges.
set -eu
version=0.4.0-dev.1
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) asset=vora-Linux-X64.tar.gz; hash=1273da18d9fdf02b4aa98970f633d6a3296bf3c90961b70886b226d8b9028ab9 ;;
  Darwin:arm64) asset=vora-macOS-ARM64.tar.gz; hash=d550a6747dbf5876e4fe3f6316c67b567186a40e00f9c2311f3501e764ebf9e8 ;;
  *) echo 'No prebuilt archive for this platform. Use the Homebrew source formula or build with CMake.' >&2; exit 1 ;;
esac
prefix=${VORA_INSTALL_DIR:-"$HOME/.local/share/vora/$version"}
if [ -e "$prefix" ] || [ -L "$prefix" ]; then
  echo "Install directory already exists: $prefix. Choose an empty VORA_INSTALL_DIR." >&2
  exit 1
fi
command -v curl >/dev/null
command -v tar >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
url="https://github.com/muneebshinwari1/vora-lang/releases/download/v$version/$asset"
curl --fail --location --proto '=https' --tlsv1.2 "$url" -o "$work/$asset"
if command -v sha256sum >/dev/null; then
  actual=$(sha256sum "$work/$asset" | cut -d ' ' -f 1)
else
  actual=$(shasum -a 256 "$work/$asset" | cut -d ' ' -f 1)
fi
if [ "$actual" != "$hash" ]; then echo 'Checksum mismatch; nothing installed.' >&2; exit 1; fi
mkdir "$work/unpack"
tar -xzf "$work/$asset" -C "$work/unpack" --strip-components=1
"$work/unpack/bin/vora" --version
"$work/unpack/bin/vora" check "$work/unpack/share/vora/examples/quickstart-fast.vora"
mkdir -p "$(dirname "$prefix")"
# The destination was checked absent; never replace an existing installation.
mkdir "$prefix"
cp -R "$work/unpack/." "$prefix/"
printf 'Installed %s in %s\nAdd %s/bin to PATH, or invoke %s/bin/vora directly.\n' "$version" "$prefix" "$prefix" "$prefix"
