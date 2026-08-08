# Changelog

## 2026-08-08

- Added the Harmony NDK C++17 repository service and ArkTS N-API boundary.
- Added real repository discovery, initialization, directory navigation and folder
  picker integration without changing the terminal UI.
- Added HEAD, branch, packed-ref, remote, index v2/v3/v4 and working-tree status reads.
- Added linked worktree `commondir` support.
- Added host-native Git repository fixtures and native-service ArkTS tests.
- Added native loose-object reads and writes for blobs, trees and commits.
- Added packed commit, tree and blob object reads with pack index v1/v2 lookup and
  OFS_DELTA/REF_DELTA reconstruction.
- Added `.gitignore`, `.git/info/exclude`, `core.excludesFile` and default global
  ignore matching to status and `git add`.
- Added real `add`, `restore`, `reset`, `commit`, `diff`, `log`, `branch`, `switch` and
  `checkout -- <path>` behavior for local repositories.
- Added `git restore --source`, combined staged/working-tree restore, `git checkout -B`
  and `git switch -C` with first-parent revision selection.
- Added index v4 compressed-path reads for status, staged and working-tree diff, and
  native operations that normalize updated indexes to v2.
- Added local `.git/config` listing, lookup, set and unset support, including subsection
  keys such as `remote.origin.url`.
- Added `HEAD` and branch reflog read/write support for supported commit, branch, checkout
  and reset operations.
- Added branch rename/copy support for `git branch -m/-M/-c/-C`.
- Added local remote management for `git remote add/remove/rename/get-url/set-url`,
  including separate push URLs.
- Added local `git rm` support with cached, forced and recursive removal.
- Added local `git mv` support that preserves unstaged working-tree changes while
  moving the original staged blob to the destination path.
- Added `git show` for `HEAD` or an explicit revision, with `--stat`, `--oneline` and
  annotated-tag peeling, plus `-- <path>` filtering.
- Added native `git cat-file` type, size, existence, pretty and explicit-type reads,
  including direct refs, abbreviated loose or packed object IDs, revision paths,
  ancestor selection and annotated-tag peel expressions.
- Added native `git hash-object` for multiple files, explicit object types and optional
  loose-object writes, with repository-subdirectory path resolution.
- Added native `git ls-tree` support for recursive, directory, tree, long, name-only,
  object-only, full-name and full-tree output, with path filtering and
  subdirectory-relative behavior.
- Added native `git ls-files` support for cached, modified, deleted, untracked and
  ignored paths, stage metadata, pathspecs and full-name output.
- Added native `git check-ignore` with verbose rule source/line output, nested ignore
  files, parent-directory exclusions, tracked-file filtering and `--no-index`.
- Added native `git show-ref` for loose, packed and symbolic references, including
  heads/tags filters, exact verification, quiet checks, tag dereference and abbreviated
  hash output.
- Added native `git symbolic-ref` reads, writes and deletion with short names,
  recursive/no-recurse resolution and reflog messages.
- Added native `git update-ref` create, compare-and-swap update, symbolic dereference,
  `--no-deref`, deletion and reflog support.
- Added loose and packed tag listing with glob filtering, lightweight and annotated tag
  creation, forced replacement and deletion. Annotated tags require `-m` until editor
  integration is available.
- Kept network transport and PTY process support explicitly outside this milestone.
