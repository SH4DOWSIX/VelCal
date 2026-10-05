#!/usr/bin/env bash
set -euo pipefail
run_id=${1:?Missing source run ID}
[[ "$run_id" =~ ^[0-9]+$ ]] || exit 1
workspace=$(cd "$(dirname "$0")/.." && pwd)
cd "$workspace"
mkdir -p .tmp
export TMPDIR="$workspace/.tmp"
sha=$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$run_id" --jq .head_sha)
[[ "$sha" =~ ^[0-9a-f]{40}$ ]] || exit 1
git fetch --no-tags origin "$sha"
if ! git diff --quiet "$sha" HEAD -- CMakeLists.txt app include src tests resources; then
    echo 'Compilation inputs changed. Request a full macOS build instead.' >&2
    exit 1
fi
gh run download "$run_id" --repo "$GITHUB_REPOSITORY" --name macos-package-inputs --dir .tmp/macos-inputs
tar -xzf .tmp/macos-inputs/macos-package-inputs.tar.gz -C "$workspace"
