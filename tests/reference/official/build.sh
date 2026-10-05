#!/bin/sh
# Build the unmodified official legacy decoder at a reproducible revision.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
revision=fc790c78c4b393f4a0101b9ad54cb3b4391037cd
source="$root/build/official-osu"
if [ ! -d "$source/.git" ]; then
    mkdir -p "$source"
    git -C "$source" init -q
    git -C "$source" remote add origin https://github.com/ppy/osu.git
    git -C "$source" fetch --depth=1 origin "$revision"
    git -C "$source" checkout --detach FETCH_HEAD
fi
[ "$(git -C "$source" rev-parse HEAD)" = "$revision" ] || {
    echo 'Official source revision does not match the pinned reference' >&2; exit 1;
}
git -C "$source" diff --exit-code HEAD --
dotnet build "$root/tests/reference/official/OfficialReference.csproj" \
    -c Release --nologo -m:4 -p:RunAnalyzers=false \
    -p:GenerateDocumentationFile=false -o "$root/build/official-reference"
