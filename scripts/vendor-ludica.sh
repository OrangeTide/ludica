#!/bin/sh
# vendor-ludica.sh — vendor a specific version of ludica into your project.
# Usage: ./vendor-ludica.sh VERSION [DEST_DIR]
#
# VERSION   CalVer tag without the 'v' prefix (e.g. 2026.04.12)
# DEST_DIR  Where to unpack (default: src/ludica)
#
# Requires: curl, tar

set -e

REPO="OrangeTide/gamedev"

version="${1:?Usage: $0 VERSION [DEST_DIR]}"
dest="${2:-src/ludica}"
tag="v${version}"
url="https://github.com/${REPO}/releases/download/${tag}/ludica-${version}.tar.gz"

echo "Fetching ludica ${version} from ${url}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

curl -fSL "$url" -o "$tmpdir/ludica.tar.gz"

mkdir -p "$dest"
tar xzf "$tmpdir/ludica.tar.gz" -C "$dest" --strip-components=1

echo "Vendored ludica ${version} into ${dest}/"
