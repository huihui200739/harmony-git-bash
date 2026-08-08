import {
  addRemote,
  checkoutBranch,
  copyBranch,
  commitRepository,
  createBranch,
  createTag,
  deleteBranch,
  deleteTags,
  directoryExists,
  diffRepository,
  getRemoteUrl,
  initializeRepository,
  inspectRepository,
  listDirectory,
  moveBranch,
  moveRepositoryPath,
  NativeCommit,
  NativeConfigEntry,
  NativeFileStatus,
  NativeRemote,
  NativeReflogEntry,
  NativeRepositoryOperation,
  NativeRepositorySnapshot,
  readConfig,
  readLog,
  readReflog,
  readTags,
  removeRepositoryPaths,
  removeRemote,
  renameRemote,
  resetHard,
  restoreFromSource,
  restoreStaged,
  restoreWorkingTree,
  setConfigValue,
  setRemoteUrl,
  showRevision,
  stageRepository,
  switchBranch,
  unsetConfigValue
} from 'libgit_native.so';
import {
  GitRepositoryService,
  RepositoryConfigEntry,
  RepositoryCommit,
  RepositoryFileStatus,
  RepositoryOperation,
  RepositoryReflogEntry,
  RepositoryRemote,
  RepositorySnapshot
} from './GitRepositoryService';

export class NativeGitRepositoryService implements GitRepositoryService {
  inspect(path: string): RepositorySnapshot {
    return this.toSnapshot(inspectRepository(path));
  }

  initialize(path: string, seedDemoFiles: boolean): RepositorySnapshot {
    return this.toSnapshot(initializeRepository(path, seedDemoFiles));
  }

  directoryExists(path: string): boolean {
    return directoryExists(path);
  }

  listDirectory(path: string): string[] {
    return listDirectory(path);
  }

  stage(path: string, paths: string[]): RepositoryOperation {
    return this.toOperation(
      stageRepository(path, paths)
    );
  }

  remove(
    path: string,
    paths: string[],
    cached: boolean,
    force: boolean,
    recursive: boolean
  ): RepositoryOperation {
    return this.toOperation(
      removeRepositoryPaths(path, paths, cached, force, recursive)
    );
  }

  movePath(
    path: string,
    source: string,
    destination: string,
    force: boolean
  ): RepositoryOperation {
    return this.toOperation(
      moveRepositoryPath(path, source, destination, force)
    );
  }

  restoreStaged(path: string, paths: string[]): RepositoryOperation {
    return this.toOperation(
      restoreStaged(path, paths)
    );
  }

  restoreWorkingTree(path: string, paths: string[]): RepositoryOperation {
    return this.toOperation(
      restoreWorkingTree(path, paths)
    );
  }

  restoreFromSource(
    path: string,
    source: string,
    paths: string[],
    staged: boolean,
    worktree: boolean
  ): RepositoryOperation {
    return this.toOperation(
      restoreFromSource(path, source, paths, staged, worktree)
    );
  }

  resetHard(path: string): RepositoryOperation {
    return this.toOperation(resetHard(path));
  }

  commit(path: string, message: string): RepositoryOperation {
    return this.toOperation(commitRepository(path, message));
  }

  createBranch(
    path: string,
    name: string,
    checkout: boolean
  ): RepositoryOperation {
    return this.toOperation(createBranch(path, name, checkout));
  }

  moveBranch(
    path: string,
    oldName: string,
    newName: string,
    force: boolean
  ): RepositoryOperation {
    return this.toOperation(
      moveBranch(path, oldName, newName, force)
    );
  }

  copyBranch(
    path: string,
    oldName: string,
    newName: string,
    force: boolean
  ): RepositoryOperation {
    return this.toOperation(
      copyBranch(path, oldName, newName, force)
    );
  }

  switchBranch(path: string, name: string): RepositoryOperation {
    return this.toOperation(switchBranch(path, name));
  }

  checkoutBranch(
    path: string,
    name: string,
    startPoint: string
  ): RepositoryOperation {
    return this.toOperation(checkoutBranch(path, name, startPoint));
  }

  deleteBranch(
    path: string,
    name: string,
    force: boolean
  ): RepositoryOperation {
    return this.toOperation(deleteBranch(path, name, force));
  }

  diff(path: string, staged: boolean): string {
    return diffRepository(path, staged);
  }

  log(path: string, maxCount: number): RepositoryCommit[] {
    return readLog(path, maxCount).map(
      (commit: NativeCommit): RepositoryCommit => ({
        id: commit.id,
        subject: commit.subject,
        author: commit.author,
        timestamp: commit.timestamp
      })
    );
  }

  show(
    path: string,
    revision: string,
    statOnly: boolean,
    oneLine: boolean
  ): string {
    return showRevision(path, revision, statOnly, oneLine);
  }

  tags(path: string): string[] {
    return readTags(path);
  }

  createTag(
    path: string,
    name: string,
    target: string,
    force: boolean,
    annotated: boolean,
    message: string
  ): RepositoryOperation {
    return this.toOperation(
      createTag(path, name, target, force, annotated, message)
    );
  }

  deleteTags(path: string, names: string[]): RepositoryOperation {
    return this.toOperation(deleteTags(path, names));
  }

  readConfig(path: string): RepositoryConfigEntry[] {
    return readConfig(path).map(
      (entry: NativeConfigEntry): RepositoryConfigEntry => ({
        key: entry.key,
        value: entry.value
      })
    );
  }

  setConfig(
    path: string,
    key: string,
    value: string
  ): RepositoryOperation {
    return this.toOperation(setConfigValue(path, key, value));
  }

  unsetConfig(path: string, key: string): RepositoryOperation {
    return this.toOperation(unsetConfigValue(path, key));
  }

  addRemote(
    path: string,
    name: string,
    url: string
  ): RepositoryOperation {
    return this.toOperation(addRemote(path, name, url));
  }

  removeRemote(path: string, name: string): RepositoryOperation {
    return this.toOperation(removeRemote(path, name));
  }

  renameRemote(
    path: string,
    oldName: string,
    newName: string
  ): RepositoryOperation {
    return this.toOperation(renameRemote(path, oldName, newName));
  }

  getRemoteUrl(path: string, name: string, push: boolean): string {
    return getRemoteUrl(path, name, push);
  }

  setRemoteUrl(
    path: string,
    name: string,
    url: string,
    push: boolean
  ): RepositoryOperation {
    return this.toOperation(setRemoteUrl(path, name, url, push));
  }

  reflog(
    path: string,
    ref: string,
    maxCount: number
  ): RepositoryReflogEntry[] {
    return readReflog(path, ref, maxCount).map(
      (entry: NativeReflogEntry): RepositoryReflogEntry => ({
        oldId: entry.oldId,
        newId: entry.newId,
        actor: entry.actor,
        timestamp: entry.timestamp,
        message: entry.message
      })
    );
  }

  private toSnapshot(nativeSnapshot: NativeRepositorySnapshot): RepositorySnapshot {
    return {
      valid: nativeSnapshot.valid,
      repositoryPath: nativeSnapshot.repositoryPath,
      gitDirectory: nativeSnapshot.gitDirectory,
      branch: nativeSnapshot.branch,
      head: nativeSnapshot.head,
      detached: nativeSnapshot.detached,
      indexVersion: nativeSnapshot.indexVersion,
      files: nativeSnapshot.files.map(
        (file: NativeFileStatus): RepositoryFileStatus => ({
          path: file.path,
          indexState: file.indexState,
          workTreeState: file.workTreeState,
          tracked: file.tracked,
          staged: file.staged
        })
      ),
      branches: nativeSnapshot.branches.slice(),
      remotes: nativeSnapshot.remotes.map(
        (remote: NativeRemote): RepositoryRemote => ({
          name: remote.name,
          fetchUrl: remote.fetchUrl,
          pushUrl: remote.pushUrl
        })
      ),
      error: nativeSnapshot.error
    };
  }

  private toOperation(
    nativeOperation: NativeRepositoryOperation
  ): RepositoryOperation {
    return {
      success: nativeOperation.success,
      changedCount: nativeOperation.changedCount,
      snapshot: this.toSnapshot(nativeOperation.snapshot),
      error: nativeOperation.error
    };
  }
}
