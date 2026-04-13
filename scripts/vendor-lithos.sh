#!/bin/sh
# vendor-lithos.sh — vendor a specific version of lithos into your project.
# Usage: ./vendor-lithos.sh VERSION [DEST_DIR]
#
# VERSION   CalVer tag without the 'v' prefix (e.g. 2026.04.12)
# DEST_DIR  Where to unpack (default: src/lithos)
#
# Requires: curl, tar

set -e

REPO="OrangeTide/gamedev"

version="${1:?Usage: $0 VERSION [DEST_DIR]}"
dest="${2:-src/lithos}"
tag="v${version}"
url="https://github.com/${REPO}/releases/download/${tag}/lithos-${version}.tar.gz"

echo "Fetching lithos ${version} from ${url}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

curl -fSL "$url" -o "$tmpdir/lithos.tar.gz"

mkdir -p "$dest"
tar xzf "$tmpdir/lithos.tar.gz" -C "$dest" --strip-components=1

echo "Vendored lithos ${version} into ${dest}/"
