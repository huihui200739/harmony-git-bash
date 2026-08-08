# Changelog

## 2026-08-08

- Added the Harmony NDK C++17 repository service and ArkTS N-API boundary.
- Added real repository discovery, initialization, directory navigation and folder
  picker integration without changing the terminal UI.
- Added HEAD, branch, packed-ref, remote, index v2/v3 and working-tree status reads.
- Added linked worktree `commondir` support.
- Added host-native Git repository fixtures and native-service ArkTS tests.
- Added native loose-object reads and writes for blobs, trees and commits.
- Added real `add`, `restore`, `reset`, `commit`, `diff`, `log`, `branch`, `switch` and
  `checkout -- <path>` behavior for local repositories.
- Kept pack-object parsing, ignore rules, network transport and PTY process support
  explicitly outside this milestone.
