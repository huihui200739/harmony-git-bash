# Harmony Git Bash

Harmony Git Bash is an in-progress native HarmonyOS PC adaptation of Git Bash. It keeps
the terminal-first Git Bash interaction model rather than replacing it with a graphical
Git client.

**Original Git Bash source repository:**
[git-for-windows/git](https://github.com/git-for-windows/git)

Git Bash is not a standalone terminal project: Git for Windows combines Git, MSYS2 and
mintty. A direct binary port is not technically valid for HarmonyOS, so this project
ports the terminal contract first and introduces a native Git service separately.

## Current implementation

- Dark MINGW64-style terminal surface for HarmonyOS PC
- Shell commands: `pwd`, `ls`, `cd`, `echo`, `printf`, `cat`, `env`, `printenv`,
  `export`, `unset`, `set`, `clear`, `help`
- Shell environment expansion for `$VAR`, `${VAR}` and `$?`, command-scoped
  assignments, `PWD`/`OLDPWD` tracking, and basic `<`, `>` and `>>` redirection
- Harmony NDK C++17 service loaded through an ArkTS N-API boundary
- Real repository discovery from a repository, nested path, picker `file://` URI or
  linked Git worktree
- Real local commands: `status`, `add`, `rm`, `mv`, `restore`, `reset`, `commit`,
  `diff`, `log`, `show`, `cat-file`, `hash-object`, `ls-tree`, `ls-files`,
  `check-ignore`, `show-ref`, `symbolic-ref`, `update-ref`, `tag`, `branch`,
  `switch`, `checkout`, `remote`, `reflog`, `rev-parse`, `init`, and `open`
- Index v2/v3/v4 parsing plus real modified, deleted and untracked working-tree status
- Loose and packed branch refs, `HEAD`, separate fetch/push URLs and worktree
  `commondir` resolution
- Loose and packed Git object read/write for blob, tree and commit operations,
  including OFS_DELTA and REF_DELTA resolution for staged and working-tree diffs
- Real index v2 writing, branch creation/switch/deletion/reset, hard reset, source
  restore and combined index/working-tree restore
- Branch rename/copy support for `git branch -m/-M/-c/-C`
- Local path removal and rename support for `git rm` and `git mv`, including cached,
  forced and recursive removal plus preservation of unstaged content during moves
- Commit display through `git show`, including `--stat`, `--oneline`, path limits and
  annotated-tag peeling
- Object inspection through `git cat-file`, including type, size, existence, pretty
  output, explicit object types, abbreviated object IDs, revision paths and tag peel
  expressions
- File hashing and optional loose-object writes through `git hash-object`, including
  multiple files, subdirectory-relative paths and explicit blob/tree/commit/tag types
- Tree listing through `git ls-tree`, including recursive, directory, tree, long,
  name-only, object-only, full-name, full-tree and path-filtered output
- Cached, modified, deleted, untracked and ignored path listing through `git ls-files`,
  including stage metadata, pathspecs and command-relative or full-name output
- Loose and packed tag listing with glob patterns, lightweight and annotated tag
  creation, editor-driven messages, forced tag replacement and tag deletion
- `.gitignore`, `.git/info/exclude`, `core.excludesFile` and default global ignore
  matching for status and `git add`
- Ignore inspection through `git check-ignore`, including verbose rule source and line
  output, tracked-file filtering, `--no-index` and subdirectory-relative paths
- Reference inspection through `git show-ref`, including heads/tags filters, `HEAD`,
  exact verification, quiet checks, annotated-tag dereference, hash-only and
  abbreviated output
- Symbolic reference reads, writes and deletion through `git symbolic-ref`, including
  short names, recursive/no-recurse resolution and reflog messages
- Atomic-style reference create, compare-and-swap update, symbolic dereference,
  `--no-deref`, deletion and reflog messages through `git update-ref`, plus
  newline- or NUL-delimited `--stdin` transactions with symbolic-ref commands,
  prepare/commit/abort status, preflight validation and filesystem rollback
- Commit graph traversal through `git rev-list`, including revision ranges,
  exclusions, namespace selectors, parent output, counts, ordering, merge filters and
  path-limited history
- Common-ancestor queries through `git merge-base`, including pair, all, octopus,
  independent, `--is-ancestor` and reflog-aware `--fork-point` modes
- Reference enumeration through `git for-each-ref`, including patterns, exclusions,
  count, formatting atoms, sorting, points-at and merged/contains filters
- Command parsing supports quoted commit messages
- System, global, local and explicit-file Git config access with `include.path`,
  conditional `gitdir`, `gitdir/i` and `onbranch` includes, scope/include controls,
  `--get-all`, `--add`, `--unset-all` and subsection keys such as
  `remote.origin.url`
- Repeated command-scoped `git -c` overrides plus `--bool`, `--int`,
  `--bool-or-int`, `--bool-or-str`, `--path`, `--expiry-date`, `--type` and
  `--default` config value handling
- Local remote management for `remote add`, `remove`, `rename`, `get-url` and
  `set-url`, including separate push URLs
- HTTPS remote reference discovery through HarmonyOS NetworkKit with
  `git ls-remote`, including heads/tags filters, patterns, peeled-ref suppression,
  symbolic `HEAD`, URL inspection and exit-code behavior
- HTTPS `git fetch` through HarmonyOS NetworkKit, including upload-pack negotiation,
  binary pack transfer, side-band progress/error handling, pack/index installation,
  transactional remote-tracking ref updates, symbolic remote `HEAD` and `FETCH_HEAD`
- HTTPS `git clone` with default destination inference, custom remote names,
  `--no-checkout`, remote default-branch selection, worktree checkout and upstream
  branch configuration
- HTTPS `git pull` for configured or explicit upstream branches, including
  up-to-date detection, fast-forward checkout and explicit refusal of divergent
  histories
- HTTPS `git push` through receive-pack, including native pack generation,
  report-status parsing, new/deleted branches, `--force`, `-u` and local
  non-fast-forward rejection
- Git pack validation and index v2 generation, including trailing SHA-1 checks,
  object/delta parsing, per-object CRCs, corruption rejection and atomic pack/index
  installation
- Local `HEAD` and branch reflog read/write for supported ref-changing operations
- Reflog management through `show`, `list`, `exists`, `write`, `delete` and `drop`,
  including numeric selectors, rewrite/updateref, dry-run/verbose output and
  single-worktree cleanup
- Reflog expiration through `expire`, including time thresholds,
  unreachable-commit pruning, stale-object fixing, rewrite/updateref, dry-run,
  verbose output and worktree scope selection
- Numeric and date-based reflog selectors, skip/since/until filtering, common
  pretty/date formatting and reflog traversal through `git log -g` and
  `git log --walk-reflogs`
- Deterministic ArkTS tests plus host-native fixtures created with system Git
- Recorded Git for Windows and mintty upstream commits plus a local refresh script

The original terminal layout, colors and MINGW64-style prompt are unchanged. Once the
native service is attached, local repository commands use the real repository on disk.
The older in-memory compatibility behavior remains covered by unit tests for the shell
surface and is used only before a native repository is opened.

## Progress snapshot

As of 2026-08-10, the functional implementation checklist is
**60/71 complete (85%)**:

- Terminal compatibility baseline: 5/5
- Native local repository backend: 43/44
- Remote transport: 7/11
- Shell and terminal parity: 5/8
- Physical HarmonyOS PC validation: 0/3

This percentage measures implemented and verified engineering work, not only UI or
project scaffolding. Signed release packaging is explicitly excluded from this
functional scope. Only completed checklist items with automated or device evidence are
counted. A previously combined config/reflog roadmap item was split into three
independently verifiable items, which corrected the denominator without changing
implemented behavior. The terminal UI remains unchanged. Physical-PC validation is
still required before the functional adaptation can be called complete.

## Current limitations

- Config conditional includes currently cover `gitdir`, case-insensitive `gitdir/i`
  and `onbranch`. Upstream `hasconfig:remote.*.url`, regular-expression lookup,
  URL matching and section rename/removal operations are not implemented yet.
- Native index writes normalize v3/v4 indexes to v2 and do not preserve optional index
  extensions such as split-index or untracked-cache data.
- Submodule materialization is rejected until native checkout supports gitlinks.
- Large pack files are read into memory per object operation; streaming and object-store
  caching remain future performance work.
- Picker URI access must still be validated on a physical HarmonyOS PC.
- HTTPS `git fetch` currently fetches advertised branch tips for one named remote.
  Explicit refspecs, pruning, tag following, shallow/filter negotiation, cancellation
  and streaming large packs are not implemented yet.
- `git clone` currently supports HTTPS repositories, default or explicit destination
  paths, custom origin names and no-checkout mode. Failed clones do not yet remove a
  newly initialized destination automatically.
- `git pull` currently performs fast-forward updates only. Three-way merge, rebase,
  autostash, explicit refspecs and conflict workflows are not implemented yet.
- `git push` currently supports HTTPS receive-pack against servers advertising
  report-status. A failed request opens a one-shot terminal username/password
  prompt, and credentials are held only in memory for the retry. Credential
  helpers, secure persistence and server-side integration against a real writable
  remote still require validation.
- Reflog walking supports numeric/date selectors, count/skip/time filters and common
  pretty/date formatting. The complete upstream `git log` option and decoration
  surface is not implemented yet.
- Command history, command/path completion and local-device copy/paste controls are
  implemented, but keyboard, IME and clipboard behavior still require validation on
  a physical HarmonyOS PC.
- Certificate policy, proxy integration, credential helpers and secure credential
  persistence are not implemented yet. Authenticated HTTPS credentials are
  supported for one-shot interactive retries and are not persisted.
- SSH transport, key handling, known hosts and passphrase prompts are not implemented.
- Annotated tags without `-m`/`--message` use the existing terminal input area as a
  message editor. Enter lines, use `:wq` to save, or `:q!`/`:cq` to cancel.
- `git ls-files --ignored` currently supports the untracked `--others` mode with
  standard repository and global excludes; tracked ignored-file queries are not yet
  implemented.
- Basic quote-aware single-line pipes can feed `echo`/`printf` output into native
  commands. `git hash-object --stdin/--stdin-paths` and
  `git check-ignore --stdin` accept newline-delimited input through this path.
- `git hash-object --path/--literally`, NUL-delimited stdin records and interactive
  input still await the PTY-backed shell input stream.
- `git show-ref --exclude-existing[=<pattern>]` accepts newline-delimited pipeline
  input and follows upstream suffix parsing, prefix filtering and existing-ref
  suppression.
- `git update-ref --stdin` accepts newline- or NUL-delimited `update`, `create`,
  `delete`, `verify`, `symref-update`, `symref-create`, `symref-delete`,
  `symref-verify`, `option no-deref`, `start`, `prepare`, `commit` and `abort`
  commands. The built-in `printf` supports NUL/octal escapes and repeated format
  use for `printf '%s\0' ... | git update-ref --stdin -z`.
  `--batch-updates` commits valid entries while reporting recoverable failures
  in Git's `rejected <ref> <new> <old> <reason>` form, including
  case-insensitive filesystem conflicts.
- `git rev-list --stdin` accepts newline-delimited revisions and paths after `--`;
  object/bisect enumeration still awaits further native graph expansion.
- `git for-each-ref --stdin` accepts newline-delimited ref patterns. Host-language
  quoting and pagination atoms still await formatter expansion.
- Pipelines and redirections are currently in-memory and single-line. Descriptor
  duplication, heredocs, command substitution, globbing, job control and PTY process
  execution are not implemented yet.

## Build

Use DevEco Studio with HarmonyOS 6.1.1 (API 24), then run:

```bash
bash ./scripts/verify.sh
```

The verification script runs host-native repository fixtures, ArkTS unit tests, both
`arm64-v8a` and `x86_64` native builds, and HAP assembly.

The unsigned development HAP is generated at:

```text
entry/build/default/outputs/default/entry-default-unsigned.hap
```

## Upstream sync

The project tracks Git for Windows as `upstream` and the user repository as `origin`.
Refresh the recorded upstream commit after reviewing changes:

```bash
bash ./scripts/update-upstream.sh
git add UPSTREAM.json
git commit -m "Update Git for Windows upstream snapshot"
```

See [docs/UPSTREAM.md](docs/UPSTREAM.md) for the attribution and synchronization policy.

## Licensing

Git and Git for Windows are licensed under GPL-2.0-only. This project is intended to be
distributed under the same license once it imports or links upstream Git code. The
initial ArkTS terminal shell contains no vendored upstream source; all future imports
must preserve upstream notices and supply corresponding source.
