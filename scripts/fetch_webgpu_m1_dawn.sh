#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 1 || -e "$1" ]]; then
    echo "Usage: $0 <new Dawn SDK directory>" >&2
    exit 1
fi

revision=e4775a0d00343d02d32daecd45e1eb683224214b
release=v20261127.173033
case "$(uname -s):$(uname -m)" in
    Linux:x86_64)
        platform=ubuntu-latest
        digest=84d837340e30063745765624e1176be4c548ab3d77f5ecf912b48dbdcfcdc060
        ;;
    Darwin:arm64)
        platform=macos-latest
        digest=5849acc2d538d7ecf4ded70f477ceaa01a81e8ffa6d4a78075bd900356776753
        ;;
    Darwin:x86_64)
        platform=macos-15-intel
        digest=3b82602a59202ca085ed40b1c6d4d48e336a54971e17b9c13d97020a5fbbe1a5
        ;;
    *)
        echo "No pinned Dawn M1 archive for this platform" >&2
        exit 1
        ;;
esac

mkdir -p "$(dirname "$1")"
scratch=$(mktemp -d "$(dirname "$1")/.dawn-m1.XXXXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
archive="$scratch/dawn.tar.gz"
curl -fLsS "https://github.com/google/dawn/releases/download/$release/Dawn-$revision-$platform-Release.tar.gz" -o "$archive"
printf '%s  %s\n' "$digest" "$archive" | shasum -a 256 -c -
mkdir "$scratch/sdk"
tar -xzf "$archive" --strip-components=1 -C "$scratch/sdk"
mv "$scratch/sdk" "$1"
