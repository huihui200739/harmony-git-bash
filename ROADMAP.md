# Roadmap

## 0.1 Terminal compatibility baseline

- [x] HarmonyOS PC ArkTS application shell
- [x] Git Bash-style terminal interaction
- [x] Common local command semantics and unit tests
- [x] Upstream commit recording and weekly change detection
- [x] Unsigned development HAP build

## 0.2 Native local repository backend

- [x] Add Harmony NDK module and native Git service boundary
- [x] Open and initialize real repositories selected through the document picker
- [x] Read HEAD, branches, remotes, index v2/v3/v4 and linked worktree metadata
- [x] Report modified, deleted and untracked working-tree files
- [x] Add host-native Git fixtures and ArkTS service-boundary tests
- [x] Implement real status, index, diff, commit, log, refs and branch operations
- [x] Add local loose-object read/write for blob, tree and commit operations
- [x] Add staged and working-tree restore, hard reset and `checkout -- <path>`
- [x] Add source/combined restore and forced branch checkout/reset
- [x] Preserve file modes, executable bits and symbolic links
- [x] Read and update local Git config, including subsection keys
- [x] Read and write `HEAD` and branch reflogs for supported ref changes
- [x] Rename and copy local branches
- [x] Add, remove, rename and update local remotes
- [x] Remove and move tracked paths with `git rm` and `git mv`
- [x] Show local revisions and peel annotated tags with `git show`
- [x] Limit `git show` by repository pathspec
- [x] Inspect loose or packed objects and revision paths with `git cat-file`
- [x] Hash files and optionally write loose objects with `git hash-object`
- [x] List commit trees recursively with `git ls-tree` options and pathspecs
- [x] List cached, modified, deleted, untracked and ignored paths with `git ls-files`
- [x] Inspect ignore matches, rule sources and tracked-file behavior with
  `git check-ignore`
- [x] Inspect loose, packed and symbolic refs with `git show-ref`
- [x] Read, write and delete symbolic refs with `git symbolic-ref`
- [x] Create, compare-and-swap, detach and delete refs with `git update-ref`
- [x] List, create, replace and delete loose or packed tags
- [x] Filter tag listings with glob patterns
- [x] Traverse commit history with `git rev-list` ranges, selectors and graph filters
- [x] Limit `git rev-list` history with regular repository pathspecs
- [x] Query common ancestors, independent tips, ancestry and reflog fork points with
  `git merge-base`
- [x] Enumerate and format refs with `git for-each-ref` filters and sorting
- [x] Feed basic `echo`/`printf` pipelines into native `hash-object`, `check-ignore`,
  `rev-list` and `for-each-ref` stdin modes
- [ ] Expand global/system config, includes, multivars and complete reflog semantics
- [x] Parse staged index differences
- [x] Apply `.gitignore`, `.git/info/exclude` and global exclude rules
- [x] Read packed commit, tree and blob objects, including delta chains
- [ ] Cross-check large and unusual repository fixtures against upstream Git

## 0.3 Remote transport

- [x] Store and inspect local remote URLs
- [ ] HTTPS clone, fetch, pull and push
- [ ] HarmonyOS certificate store integration
- [ ] SSH keys, known hosts and passphrase prompts
- [ ] Proxy, timeout, progress and cancellation behavior
- [ ] Credential redaction and secure persistence

## 0.4 Shell and terminal parity

- [x] Basic quote-aware, in-memory single-line pipelines
- [ ] PTY-backed process session
- [ ] Bash-compatible quoting, pipes, redirection and environment variables
- [ ] Command history, completion, selection, copy and paste
- [ ] ANSI colors, cursor control and resize handling
- [ ] Git credential and editor prompt integration
- [ ] Editor-driven annotated tag messages

## 1.0 Device validation

- [ ] HarmonyOS PC keyboard, IME, clipboard and window behavior
- [ ] Large repositories and long-path tests
- [ ] Interrupted network and repository recovery tests
- [ ] Signed release HAP and installation documentation
