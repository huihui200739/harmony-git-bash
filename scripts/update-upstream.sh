#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

resolve_ref() {
  local slug="$1"
  local repository="$2"
  local reference="$3"
  local commit=""
  local branch="${reference#refs/heads/}"
  if command -v gh >/dev/null 2>&1 && gh auth status >/dev/null 2>&1; then
    commit="$(gh api "repos/$slug/commits/$branch" --jq .sha)"
    printf '%s' "$commit"
    return
  fi
  for attempt in 1 2 3; do
    commit="$(git -c http.version=HTTP/1.1 ls-remote "$repository" "$reference" | awk '{print $1}')" && break
    sleep "$attempt"
  done
  printf '%s' "$commit"
}

git_for_windows_commit="$(resolve_ref git-for-windows/git https://github.com/git-for-windows/git.git refs/heads/main)"
mintty_commit="$(resolve_ref mintty/mintty https://github.com/mintty/mintty.git refs/heads/master)"

if [[ -z "$git_for_windows_commit" || -z "$mintty_commit" ]]; then
  echo "Unable to resolve an upstream reference." >&2
  exit 1
fi

node -e '
const fs = require("fs");
const file = "UPSTREAM.json";
const data = JSON.parse(fs.readFileSync(file, "utf8"));
data.checkedAt = new Date().toISOString();
data.gitForWindows.commit = process.argv[1];
data.mintty.commit = process.argv[2];
fs.writeFileSync(file, `${JSON.stringify(data, null, 2)}\n`);
' "$git_for_windows_commit" "$mintty_commit"

echo "Updated UPSTREAM.json"
