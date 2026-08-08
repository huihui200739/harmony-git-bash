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
- Git compatibility session: `status`, `add`, `restore --staged`, `reset`, `commit -m`,
  `log`, `branch`, `switch`, `checkout`, `diff`, `remote`, `init`
- Command parsing supports quoted commit messages
- Deterministic tests for status, staging, commits, branches and protected destructive
  operations
- Recorded Git for Windows and mintty upstream commits plus a local refresh script

## Native port plan

The command surface is deliberately separated from the future native backend. The next
milestone is to add a Harmony NDK Git service for real local repositories, file
operations, commits and branch refs. Network transport (`clone`, `fetch`, `pull`, and
`push`) follows after certificate storage, SSH credential handling and HarmonyOS network
permissions have been implemented and device-tested.

The current compatibility session refuses destructive file operations such as
`git reset --hard`; it does not falsely claim to modify files before the native service
exists.

## Build

Use DevEco Studio with HarmonyOS 6.1.1 (API 24), then run:

```bash
bash ./scripts/verify.sh
```

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
