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
- Shell commands: `pwd`, `ls`, `cd`, `clear`, `help`
- Harmony NDK C++17 service loaded through an ArkTS N-API boundary
- Real repository discovery from a repository, nested path, picker `file://` URI or
  linked Git worktree
- Real local commands: `status`, `add`, `rm`, `mv`, `restore`, `reset`, `commit`,
  `diff`, `log`, `show`, `cat-file`, `hash-object`, `ls-tree`, `ls-files`,
  `check-ignore`, `show-ref`, `symbolic-ref`, `update-ref`, `tag`, `branch`,
  `switch`, `checkout`, `remote`, `rev-parse`, `init`, and `open`
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
  creation, forced tag replacement and tag deletion
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
- Local `.git/config` listing, lookup, set and unset support, including subsection
  keys such as `remote.origin.url`
- Local remote management for `remote add`, `remove`, `rename`, `get-url` and
  `set-url`, including separate push URLs
- Local `HEAD` and branch reflog read/write for supported ref-changing operations
- Deterministic ArkTS tests plus host-native fixtures created with system Git
- Recorded Git for Windows and mintty upstream commits plus a local refresh script

The original terminal layout, colors and MINGW64-style prompt are unchanged. Once the
native service is attached, local repository commands use the real repository on disk.
The older in-memory compatibility behavior remains covered by unit tests for the shell
surface and is used only before a native repository is opened.

## Current limitations

- Config support is intentionally local and small: global/system config discovery,
  includes, command-scoped config, multivars and every Git config value type are not
  implemented yet.
- Native index writes normalize v3/v4 indexes to v2 and do not preserve optional index
  extensions such as split-index or untracked-cache data.
- Submodule materialization is rejected until native checkout supports gitlinks.
- Large pack files are read into memory per object operation; streaming and object-store
  caching remain future performance work.
- Picker URI access must still be validated on a physical HarmonyOS PC.
- `clone`, `fetch`, `pull` and `push` await certificate, SSH credential and network
  permission integration.
- Remote configuration is local-only for now; it does not yet perform network
  transport or remote-tracking ref synchronization.
- Annotated tag creation currently requires `-m`/`--message` because editor prompt
  integration is not available yet.
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
  `--batch-updates` remains future work.
- `git rev-list --stdin` accepts newline-delimited revisions and paths after `--`;
  object/bisect enumeration still awaits further native graph expansion.
- `git for-each-ref --stdin` accepts newline-delimited ref patterns. Host-language
  quoting and pagination atoms still await formatter expansion.
- Pipelines are currently in-memory and single-line; redirection, environment
  expansion, job control and PTY process execution are not implemented yet.

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
