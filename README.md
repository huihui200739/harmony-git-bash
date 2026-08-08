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
- Real local commands: `status`, `add`, `restore`, `reset`, `commit`, `diff`, `log`,
  `branch`, `switch`, `checkout`, `remote`, `rev-parse`, `init`, and `open`
- Index v2/v3 parsing plus real modified, deleted and untracked working-tree status
- Loose and packed branch refs, `HEAD`, separate fetch/push URLs and worktree
  `commondir` resolution
- Loose and packed Git object read/write for blob, tree and commit operations,
  including OFS_DELTA and REF_DELTA resolution for staged and working-tree diffs
- Real index v2 writing, branch creation/switch/deletion/reset, hard reset, source
  restore and combined index/working-tree restore
- `.gitignore`, `.git/info/exclude`, `core.excludesFile` and default global ignore
  matching for status and `git add`
- Command parsing supports quoted commit messages
- Deterministic ArkTS tests plus host-native fixtures created with system Git
- Recorded Git for Windows and mintty upstream commits plus a local refresh script

The original terminal layout, colors and MINGW64-style prompt are unchanged. Once the
native service is attached, local repository commands use the real repository on disk.
The older in-memory compatibility behavior remains covered by unit tests for the shell
surface and is used only before a native repository is opened.

## Current limitations

- Git index v4 path compression is rejected with an explicit error.
- Global config discovery is intentionally small and does not yet implement includes,
  command-scoped config, or every Git config value type.
- Submodule materialization is rejected until native checkout supports gitlinks.
- Large pack files are read into memory per object operation; streaming and object-store
  caching remain future performance work.
- Picker URI access must still be validated on a physical HarmonyOS PC.
- `clone`, `fetch`, `pull` and `push` await certificate, SSH credential and network
  permission integration.

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
