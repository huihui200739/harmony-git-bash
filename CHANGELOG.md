# Changelog

## 2026-08-11

- Added explicit physical Enter-key command submission for HarmonyOS PC keyboards
  while preserving the existing Git Bash terminal UI and IME `.onSubmit()` path.
  The PC emulator exposed that raw hardware Enter events do not always invoke
  `TextInput.onSubmit()`. Full keyboard, IME, clipboard and window validation
  remains open for a physical HarmonyOS PC, so verified functional progress
  remains 65/71 (92%).
- Added Bash-style `IFS` field splitting for unquoted variable and command
  substitutions while preserving quoted and assignment values, including empty-field
  behavior, prefix/suffix concatenation, pathname expansion and ambiguous redirect
  detection. Added common integer arithmetic expansion with precedence, parentheses,
  variables, decimal/hexadecimal values and divide-by-zero errors across regular
  commands, command substitutions, heredocs and command-scoped assignments. Assignment
  expansion now follows Bash ordering, the terminal UI is unchanged, and the upstream
  refresh script now resolves Git for Windows, Git and mintty on every check. All three
  upstream commit snapshots remain unchanged. Native fixtures, ArkTS tests, both native
  ABIs and unsigned HAP assembly pass. Complete Bash parameter/quote compatibility and
  PTY execution remain open, so verified functional progress remains 65/71 (92%).
- Added interactive in-memory heredocs through the unchanged Git Bash terminal UI.
  `<<` and tab-stripping `<<-` use the Bash-style `>` continuation prompt, quoted
  delimiters preserve literal content, unquoted bodies expand supported variables
  and command substitutions, and heredoc stdin works with pipelines and file
  redirection. `Ctrl+C` cancels the pending heredoc session. The Git for Windows,
  Git and mintty upstream commit snapshots did not change. Native fixtures, ArkTS
  tests, both native ABIs and unsigned HAP assembly pass. Complete Bash
  quote/field-splitting semantics and PTY execution remain open, so verified
  functional progress remains 65/71 (92%).

## 2026-08-10

- Added nested `$(...)` and backtick command substitution for the supported local
  shell/Git command surface, with trailing-newline removal and restoration of the
  parent shell directory/environment after each substitution. Added unquoted `*`,
  `?` and bracket pathname expansion through the native directory service, including
  leading-dot filtering, quoted/escaped suppression and unchanged unmatched patterns.
  Pipeline parsing now keeps substitution-internal pipes together and incomplete
  quotes, escapes and substitutions report syntax failures. The Git Bash terminal UI
  is unchanged. Native fixtures, ArkTS tests, both native ABIs and unsigned HAP
  assembly pass. The broader Bash-compatibility roadmap item remains open for field
  splitting, descriptor redirection, heredocs and PTY execution, so verified
  functional progress remains 65/71 (92%).
- Added one enforced HarmonyOS TLS policy across HTTPS advertisement, upload-pack
  and receive-pack requests without changing the Git Bash terminal UI. NetworkKit
  uses system CA validation by default, `http.sslCAInfo` resolves absolute,
  repository-relative and home-relative custom CA paths, and inaccessible CA files
  are rejected at the native boundary. `http.sslVerify=false` is explicitly refused
  instead of using NetworkKit's insecure skip-validation mode. Host-native fixtures,
  ArkTS propagation tests and the unsigned HAP build pass; real-device certificate
  store behavior remains a physical HarmonyOS PC validation boundary. Verified
  functional progress is now 65/71 (92%).
- Added ANSI terminal rendering for standard and bright foreground colors,
  xterm 256-color and truecolor sequences, bold state, cursor movement,
  carriage-return progress updates, line/display erasure and save/restore
  controls. The terminal viewport now updates shell `COLUMNS` and `LINES`
  from its ArkUI size while keeping the existing Git Bash terminal UI
  unchanged. Added ArkTS coverage for ANSI parsing and resize calculations.
  The Git for Windows, Git and mintty upstream snapshots had no commit
  changes in this refresh; verified functional progress is now 64/71 (90%).
- Added HTTPS credential redaction and secure persistence without changing the
  Git Bash terminal UI. Embedded URL credentials, Basic/Bearer authorization
  values and password/token/secret options are removed from command history,
  displayed output and copyable terminal lines. Successful credentials are
  stored through HarmonyOS AssetStoreKit with a process-memory fallback when
  the secure asset service is unavailable; stale stored credentials are removed
  after a 401 response, and remote URLs are sanitized before native persistence.
  Verified functional progress is now 63/71 (89%). Real-device persistence
  validation, certificate policy, proxy authentication and SSH transport remain
  outstanding.
- Added verified HTTPS transport controls for HarmonyOS remote operations:
  system/disabled/custom proxy modes, proxy exclusions, connect/read timeouts,
  upload/download progress callbacks and cancellation through `Ctrl+C`.
  The terminal UI remains unchanged; progress is collected for the shell/service
  boundary without adding a separate progress display.
- Certificate policy, proxy authentication and SSH transport remain outside this
  completed batch.
- Completed the native-metadata `reflog show` and `git log -g` option batch:
  `-<count>`/`-n<count>`, configurable abbreviations, built-in
  `short`/`medium`/`full`/`fuller`/`reference` formats, additional identity/date
  placeholders, sanitized subjects and Git-compatible validation for invalid
  counts, formats, dates, `--reverse` and `--grep-reflog`. The terminal UI is
  unchanged. Verified functional progress is now 61/71 (86%).
- Added one-shot HTTPS credential prompts for `ls-remote`, `fetch`, `clone`,
  `pull` and `push`. Usernames are preserved exactly, passwords are masked and
  excluded from history/copy operations, credentials are passed through as
  Basic Auth for the retry, and failed credentials do not trigger an infinite
  retry loop. Verified functional progress is now 60/71 (85%); secure
  persistence and physical HarmonyOS PC validation remain outstanding.
- Added terminal-driven annotated tag messages with `git tag -a`, including
  multi-line editing, `:wq` save, `:q!`/`:cq` cancellation, `-F` file messages,
  multiple `-m` paragraphs and Git-style cleanup modes.
- Advanced verified functional progress to 59/71 (83%) and refreshed the mintty
  upstream snapshot to `5a8e221efb311570ef2a612e6092b19569ef0e07`.
- Added repeated top-level `git -c` overrides, bare-key boolean values and guaranteed
  cleanup after synchronous and asynchronous clone/fetch/pull/push commands.
- Added conditional config includes for canonicalized `gitdir`, case-insensitive
  `gitdir/i` and `onbranch` matches, including linked and symlinked path coverage.
- Added `git config --file`/`-f` routing plus boolean, integer, bool-or-int,
  bool-or-string, path, expiry-date, `--type` and `--default` handling across
  ArkTS, N-API and the native service.
- Added system-Git comparison fixtures and ArkTS regression coverage for command
  overrides, conditional includes, explicit files and typed config values.
- Hardened `scripts/verify.sh` so ArkTS assertion failures cannot be hidden by a
  successful `hvigor` process exit code.
- Added command history with draft restoration and deduplication, shell/Git/path
  completion, and local-device copy/paste controls without changing the terminal UI.
- Added reflog author/committer metadata and Git log/reflog filters for grep,
  author/committer, regular-expression and all-match modes.
- Advanced verified functional progress to 58/71 (82%) without changing the
  Git Bash terminal UI.
- Added numeric and date-based reflog selectors, skip/since/until filters, common
  pretty/date formatting and reflog walking through `git log -g` and
  `git log --walk-reflogs`.
- Split the combined config/reflog roadmap entry into independently verifiable
  items and corrected functional progress to 56/71 (79%).
- Added native `git reflog list`, `exists`, `write`, `delete` and `drop` support
  without changing the Git Bash terminal UI.
- Added numeric reflog selectors, message normalization, exact-ref checks,
  full object-ID validation, `--rewrite`, `--updateref`, `--dry-run`, `--verbose`,
  `--all` and `--single-worktree`.
- Added linked-worktree reflog path handling and system-Git comparison coverage
  for listing, deletion, reference updates and worktree-scoped cleanup.
- Added native `git reflog expire` support for time thresholds,
  unreachable and stale-object pruning, rewrite/updateref, dry-run/verbose
  output and linked-worktree scope selection, with system-Git fixture coverage.
- Signed release HAP packaging remains excluded from the functional progress scope.

## 2026-08-09

- Added system/global/local Git config discovery, relative and home-based
  `include.path` loading, include controls, effective-value precedence, `--get-all`,
  `--add`, `--unset-all` and duplicate-value protection through the unchanged
  terminal UI.
- Added native system-Git comparisons for scoped config reads/writes, includes,
  multivars and global commit identity, plus ArkTS routing coverage.
- Advanced verified functional progress to 53/67 (79%).
- Added a system-Git comparison fixture with 640 bulk files, a path longer than
  220 characters, space and dash-prefixed names, packed branch refs and repacked
  objects; native `ls-files`, recursive `ls-tree`, status, branch and object reads
  now agree with upstream Git for that fixture.
- Corrected the verified functional progress to 52/66 (79%); signed release
  packaging is tracked separately and excluded from the requested adaptation scope.
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

## 2026-08-10

- Added ordered in-memory `0/1/2` descriptor redirection, including `2>&1`,
  `1>&2`, `&>`/`&>>`, descriptor closing, `/dev/null`, and stderr isolation
  across pipelines. The broader Bash parity item remains incomplete because
  heredocs, full quote/field splitting, job control and PTY execution are still
  outside the verified boundary, so functional progress remains 65/71 (92%).
