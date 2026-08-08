import {
  checkoutBranch,
  commitRepository,
  createBranch,
  deleteBranch,
  directoryExists,
  diffRepository,
  initializeRepository,
  inspectRepository,
  listDirectory,
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
  resetHard,
  restoreFromSource,
  restoreStaged,
  restoreWorkingTree,
  setConfigValue,
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
