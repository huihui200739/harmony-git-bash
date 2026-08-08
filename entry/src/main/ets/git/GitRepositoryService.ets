export interface RepositoryFileStatus {
  path: string;
  indexState: string;
  workTreeState: string;
  tracked: boolean;
  staged: boolean;
}

export interface RepositoryRemote {
  name: string;
  fetchUrl: string;
  pushUrl: string;
}

export interface RepositorySnapshot {
  valid: boolean;
  repositoryPath: string;
  gitDirectory: string;
  branch: string;
  head: string;
  detached: boolean;
  indexVersion: number;
  files: RepositoryFileStatus[];
  branches: string[];
  remotes: RepositoryRemote[];
  error: string;
}

export interface RepositoryOperation {
  success: boolean;
  changedCount: number;
  snapshot: RepositorySnapshot;
  error: string;
}

export interface RepositoryCommit {
  id: string;
  subject: string;
  author: string;
  timestamp: string;
}

export interface RepositoryConfigEntry {
  key: string;
  value: string;
}

export interface RepositoryReflogEntry {
  oldId: string;
  newId: string;
  actor: string;
  timestamp: string;
  message: string;
}

export interface GitRepositoryService {
  inspect(path: string): RepositorySnapshot;
  initialize(path: string, seedDemoFiles: boolean): RepositorySnapshot;
  directoryExists(path: string): boolean;
  listDirectory(path: string): string[];
  stage(path: string, paths: string[]): RepositoryOperation;
  restoreStaged(path: string, paths: string[]): RepositoryOperation;
  restoreWorkingTree(path: string, paths: string[]): RepositoryOperation;
  restoreFromSource(
    path: string,
    source: string,
    paths: string[],
    staged: boolean,
    worktree: boolean
  ): RepositoryOperation;
  resetHard(path: string): RepositoryOperation;
  commit(path: string, message: string): RepositoryOperation;
  createBranch(
    path: string,
    name: string,
    checkout: boolean
  ): RepositoryOperation;
  switchBranch(path: string, name: string): RepositoryOperation;
  checkoutBranch(
    path: string,
    name: string,
    startPoint: string
  ): RepositoryOperation;
  deleteBranch(
    path: string,
    name: string,
    force: boolean
  ): RepositoryOperation;
  diff(path: string, staged: boolean): string;
  log(path: string, maxCount: number): RepositoryCommit[];
  readConfig(path: string): RepositoryConfigEntry[];
  setConfig(
    path: string,
    key: string,
    value: string
  ): RepositoryOperation;
  unsetConfig(path: string, key: string): RepositoryOperation;
  reflog(
    path: string,
    ref: string,
    maxCount: number
  ): RepositoryReflogEntry[];
}
