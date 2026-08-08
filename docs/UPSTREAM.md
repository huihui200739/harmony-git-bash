# Upstream relationship

Git Bash is shipped as part of Git for Windows. The primary source reference for this
project is [git-for-windows/git](https://github.com/git-for-windows/git), whose current
`main` snapshot is recorded in `UPSTREAM.json`.

The Git command implementation is derived from the Git project:
[git/git](https://github.com/git/git). Git for Windows also relies on the MSYS2 runtime
and [mintty](https://github.com/mintty/mintty) terminal emulator.

This repository is a HarmonyOS PC porting project, not a replacement Windows build. The
ArkTS terminal and compatibility model are original project code. Before importing or
linking any upstream C source, the importer must preserve its headers, GPL-2.0-only
licensing obligations, attribution and source-distribution requirements.

## Sync policy

- Local development configures `upstream` as `https://github.com/git-for-windows/git.git`.
- `scripts/update-upstream.sh` fetches the current upstream heads and refreshes
  `UPSTREAM.json` after review.
- The repository deliberately does not auto-merge upstream C changes into an unreviewed
  HarmonyOS port. A scheduled GitHub Action can be added after the publishing credential
  has the GitHub `workflow` scope.
