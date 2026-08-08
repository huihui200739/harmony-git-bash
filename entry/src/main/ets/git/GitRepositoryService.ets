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

export interface RepositoryListFilesOptions {
  cached: boolean;
  modified: boolean;
  deleted: boolean;
  others: boolean;
  ignored: boolean;
  excludeStandard: boolean;
  stage: boolean;
  fullName: boolean;
  paths: string[];
}

export interface RepositoryListTreeOptions {
  recursive: boolean;
  directoriesOnly: boolean;
  includeTrees: boolean;
  nameOnly: boolean;
  objectOnly: boolean;
  longFormat: boolean;
  fullName: boolean;
  fullTree: boolean;
  paths: string[];
}

export interface GitRepositoryService {
  inspect(path: string): RepositorySnapshot;
  initialize(path: string, seedDemoFiles: boolean): RepositorySnapshot;
  directoryExists(path: string): boolean;
  listDirectory(path: string): string[];
  stage(path: string, paths: string[]): RepositoryOperation;
  remove(
    path: string,
    paths: string[],
    cached: boolean,
    force: boolean,
    recursive: boolean
  ): RepositoryOperation;
  movePath(
    path: string,
    source: string,
    destination: string,
    force: boolean
  ): RepositoryOperation;
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
  moveBranch(
    path: string,
    oldName: string,
    newName: string,
    force: boolean
  ): RepositoryOperation;
  copyBranch(
    path: string,
    oldName: string,
    newName: string,
    force: boolean
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
  show(
    path: string,
    revision: string,
    statOnly: boolean,
    oneLine: boolean,
    paths: string[]
  ): string;
  tags(path: string, patterns: string[]): string[];
  listFiles(
    path: string,
    options: RepositoryListFilesOptions
  ): string[];
  hashObject(
    path: string,
    paths: string[],
    type: string,
    write: boolean
  ): string[];
  checkIgnore(
    path: string,
    paths: string[],
    noIndex: boolean,
    verbose: boolean
  ): string[];
  catFile(
    path: string,
    objectName: string,
    mode: string
  ): string;
  listTree(
    path: string,
    treeish: string,
    options: RepositoryListTreeOptions
  ): string[];
  createTag(
    path: string,
    name: string,
    target: string,
    force: boolean,
    annotated: boolean,
    message: string
  ): RepositoryOperation;
  deleteTags(path: string, names: string[]): RepositoryOperation;
  readConfig(path: string): RepositoryConfigEntry[];
  setConfig(
    path: string,
    key: string,
    value: string
  ): RepositoryOperation;
  unsetConfig(path: string, key: string): RepositoryOperation;
  addRemote(
    path: string,
    name: string,
    url: string
  ): RepositoryOperation;
  removeRemote(path: string, name: string): RepositoryOperation;
  renameRemote(
    path: string,
    oldName: string,
    newName: string
  ): RepositoryOperation;
  getRemoteUrl(path: string, name: string, push: boolean): string;
  setRemoteUrl(
    path: string,
    name: string,
    url: string,
    push: boolean
  ): RepositoryOperation;
  reflog(
    path: string,
    ref: string,
    maxCount: number
  ): RepositoryReflogEntry[];
}
