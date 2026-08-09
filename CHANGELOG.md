# Changelog

## 2026-08-09

- Added asynchronous HTTPS `git push` through receive-pack, including native
  commit/tree/blob/tag pack generation, report-status parsing, new and deleted
  branches, `--force`, `-u` upstream configuration and local non-fast-forward
  rejection without changing the terminal UI.
- Added asynchronous HTTPS `git clone` with default destination inference, `--origin`,
  `--no-checkout`, remote `HEAD` selection, worktree materialization and upstream
  branch configuration without changing the terminal UI.
- Added asynchronous HTTPS `git pull` with configured or explicit upstream selection,
  up-to-date detection, fast-forward checkout and divergent-history refusal.
- Added ArkTS service-boundary coverage for clone destinations, custom remotes,
  no-checkout refs, upstream config, fast-forward pulls and failure states.
- Added asynchronous HTTPS `git fetch` through HarmonyOS NetworkKit without changing
  the existing terminal layout or styling.
- Added binary upload-pack POST requests, advertisement capability forwarding,
  ACK/NAK parsing, side-band progress/error handling and up-to-date responses without
  a pack.
- Added native pack header and trailing SHA-1 validation, object/delta parsing,
  per-object CRC calculation and pack index v2 generation.
- Added atomic `.pack`/`.idx` installation, transactional remote-tracking ref updates,
  symbolic remote `HEAD` and `FETCH_HEAD`.
- Added real system-Git pack fixtures, `git fsck --full` validation, native object-read
  checks, duplicate-object coverage and corrupt-pack rejection.
- Added newline-delimited native `git update-ref --stdin` transactions for `update`,
  `create`, `delete`, `verify`, `option no-deref`, `start`, `prepare`, `commit` and
  `abort`.
- Added C-style quoted transaction fields, standard and octal escapes,
  `--create-reflog`, pre-transaction object/ref validation and duplicate
  dereferenced-ref rejection.
- Added loose-ref, packed-ref and reflog backups so a filesystem failure during a
  transaction restores already-applied updates.
- Routed pipeline input and transaction status output through N-API, ArkTS and the
  unchanged Git Bash terminal surface; `-z` and `--batch-updates` now report explicit
  unsupported-mode errors.
- Added native `git show-ref --exclude-existing[=<pattern>]` filtering for
  newline-delimited pipeline input, including peeled suffix removal, prefix
  selection, existing-ref suppression and invalid-ref warnings.
- Added upstream Git comparison fixtures and ArkTS routing coverage for
  `show-ref --exclude-existing`.
- Added quote-aware, single-line in-memory pipelines with basic `echo` and `printf`
  builtins without changing the terminal UI.
- Added stdin hashing and optional loose-object writes for
  `git hash-object --stdin`, plus newline-delimited `--stdin-paths`.
- Added newline-delimited stdin routing for `git check-ignore`, `git rev-list` and
  `git for-each-ref`, including the `rev-list` stdin `--` path separator.
- Added native and ArkTS regression coverage for pipeline routing and stdin object
  compatibility with system Git.
- Added native `git rev-list` support for revision ranges, exclusions, all/branch/tag/
  remote selectors, parent output, counts, reverse order, first-parent traversal,
  merge filters, abbreviated commit output and path-limited history.
- Added native `git merge-base` support for pairwise, `--all`, `--octopus`,
  `--independent`, `--is-ancestor` and reflog-aware `--fork-point` queries with
  exit-status propagation.
- Added native `git for-each-ref` support for ref patterns/exclusions, counts, sorting,
  common ref/object/identity atoms, points-at and merged/contains filters.
- Added recursive symbolic-reference resolution with cycle detection for packed and
  loose references.
- Added system-Git comparison fixtures covering commit graph traversal, annotated tags,
  packed refs and remote symbolic refs.

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
