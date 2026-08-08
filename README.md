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
- Real read commands: `status`, `branch`, `remote`, `rev-parse`, `init`, and `open`
- Index v2/v3 parsing plus real modified, deleted and untracked working-tree status
- Loose and packed branch refs, `HEAD`, separate fetch/push URLs and worktree
  `commondir` resolution
- Command parsing supports quoted commit messages
- Deterministic ArkTS tests plus host-native fixtures created with system Git
- Recorded Git for Windows and mintty upstream commits plus a local refresh script

The original terminal layout, colors and MINGW64-style prompt are unchanged. The current
native milestone intentionally refuses `add`, `commit`, `diff`, `log`, `switch`,
`checkout`, `restore`, and `reset` against a real repository until their object/index
write implementations are complete. The older in-memory compatibility behavior remains
covered by unit tests but is not used after the app attaches the native service.

## Current limitations

- Staged differences between an existing `HEAD` tree and the index are not yet computed.
- Git index v4 path compression is rejected with an explicit error.
- `.gitignore` and exclude rules are not yet applied to untracked-file discovery.
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
