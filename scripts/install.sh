#!/bin/sh
# Install the fixed, checksum-verified release without root privileges.
set -eu
version=0.4.0
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) asset=vora-Linux-X64.tar.gz; hash=633499c3c921ff34f98e6edeec63b6d4a1f4f3af92cd63b127a0134393bf58bb ;;
  Darwin:arm64) asset=vora-macOS-ARM64.tar.gz; hash=5d1d005c05193f22c13f4a0587f2ef4576f1dd6ea5559371e41c81925028361a ;;
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
"$work/unpack/bin/vora" --help
"$work/unpack/bin/vora" check "$work/unpack/share/vora/examples/quickstart-fast.vora"
mkdir -p "$(dirname "$prefix")"
# The destination was checked absent; never replace an existing installation.
mkdir "$prefix"
cp -R "$work/unpack/." "$prefix/"
printf 'Installed %s in %s\nAdd %s/bin to PATH, or invoke %s/bin/vora directly.\n' "$version" "$prefix" "$prefix" "$prefix"
