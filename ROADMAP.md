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
- [ ] Expand Git config, ref and reflog semantics
- [x] Parse staged index differences
- [x] Apply `.gitignore`, `.git/info/exclude` and global exclude rules
- [x] Read packed commit, tree and blob objects, including delta chains
- [ ] Cross-check large and unusual repository fixtures against upstream Git

## 0.3 Remote transport

- [ ] HTTPS clone, fetch, pull and push
- [ ] HarmonyOS certificate store integration
- [ ] SSH keys, known hosts and passphrase prompts
- [ ] Proxy, timeout, progress and cancellation behavior
- [ ] Credential redaction and secure persistence

## 0.4 Shell and terminal parity

- [ ] PTY-backed process session
- [ ] Bash-compatible quoting, pipes, redirection and environment variables
- [ ] Command history, completion, selection, copy and paste
- [ ] ANSI colors, cursor control and resize handling
- [ ] Git credential and editor prompt integration

## 1.0 Device validation

- [ ] HarmonyOS PC keyboard, IME, clipboard and window behavior
- [ ] Large repositories and long-path tests
- [ ] Interrupted network and repository recovery tests
- [ ] Signed release HAP and installation documentation
