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

export interface RepositoryRemoteReference {
  objectId: string;
  name: string;
}

export interface RepositoryRemoteAdvertisement {
  success: boolean;
  responseCode: number;
  headTarget: string;
  references: RepositoryRemoteReference[];
  capabilities: string[];
  error: string;
}

export interface RepositoryCredentials {
  username: string;
  password: string;
}

export interface RepositoryRemotePackResponse {
  success: boolean;
  acknowledged: boolean;
  objectCount: number;
  packData: ArrayBuffer;
  progress: string;
  error: string;
}

export interface RepositoryRemotePushUpdate {
  oldObjectId: string;
  newObjectId: string;
  name: string;
}

export interface RepositoryRemotePushResult {
  success: boolean;
  unpacked: boolean;
  output: string[];
  error: string;
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
  output: string[];
  error: string;
  responseCode?: number;
}

export interface RepositoryCleanOptions {
  dryRun: boolean;
  directories: boolean;
  quiet: boolean;
  removeIgnored: boolean;
  ignoredOnly: boolean;
  force: number;
  excludes: string[];
  paths: string[];
}

export interface RepositoryCleanResult {
  success: boolean;
  changedCount: number;
  cleanedPaths: string[];
  skippedRepositories: string[];
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
  index: number;
  selector: string;
  subject: string;
  author: string;
  committer: string;
  authorTimestamp: string;
  commitTimestamp: string;
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

export interface RepositoryShowRefOptions {
  heads: boolean;
  tags: boolean;
  includeHead: boolean;
  dereference: boolean;
  verify: boolean;
  quiet: boolean;
  hashOnly: boolean;
  abbreviation: number;
  patterns: string[];
}

export interface RepositoryRevListOptions {
  all: boolean;
  branches: boolean;
  tags: boolean;
  remotes: boolean;
  parents: boolean;
  count: boolean;
  reverse: boolean;
  firstParent: boolean;
  noMerges: boolean;
  merges: boolean;
  abbreviate: boolean;
  abbreviation: number;
  maxCount: number;
  revisions: string[];
  paths: string[];
}

export interface RepositoryMergeBaseOptions {
  all: boolean;
  octopus: boolean;
  independent: boolean;
  revisions: string[];
}

export interface RepositoryForEachRefOptions {
  count: number;
  format: string;
  sortKeys: string[];
  patterns: string[];
  excludes: string[];
  pointsAt: string;
  merged: string;
  noMerged: string;
  contains: string;
  noContains: string;
  ignoreCase: boolean;
  includeRootRefs: boolean;
}

export interface GitRepositoryService {
  inspect(path: string): RepositorySnapshot;
  initialize(path: string, seedDemoFiles: boolean): RepositorySnapshot;
  directoryExists(path: string): boolean;
  listDirectory(path: string): string[];
  readFile(path: string, filePath: string): string;
  writeFile(
    path: string,
    filePath: string,
    content: string,
    append: boolean
  ): RepositoryOperation;
  clean(
    path: string,
    options: RepositoryCleanOptions
  ): RepositoryCleanResult;
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
  hashInput(
    path: string,
    input: string,
    type: string,
    write: boolean
  ): string;
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
  showRef(
    path: string,
    options: RepositoryShowRefOptions
  ): string[];
  excludeExistingRefs(
    path: string,
    input: string,
    pattern: string
  ): string[];
  revList(
    path: string,
    options: RepositoryRevListOptions
  ): string[];
  mergeBase(
    path: string,
    options: RepositoryMergeBaseOptions
  ): string[];
  isAncestor(
    path: string,
    ancestor: string,
    descendant: string
  ): boolean;
  findForkPoint(
    path: string,
    reference: string,
    derived: string
  ): string;
  forEachRef(
    path: string,
    options: RepositoryForEachRefOptions
  ): string[];
  symbolicRef(
    path: string,
    name: string,
    shortName: boolean,
    recurse: boolean
  ): string;
  updateSymbolicRef(
    path: string,
    name: string,
    target: string,
    deleteReference: boolean,
    message: string
  ): RepositoryOperation;
  updateRef(
    path: string,
    name: string,
    newValue: string,
    oldValue: string,
    deleteReference: boolean,
    noDeref: boolean,
    message: string,
    createReflog: boolean
  ): RepositoryOperation;
  updateRefs(
    path: string,
    input: string,
    noDeref: boolean,
    createReflog: boolean,
    message: string,
    nullTerminated: boolean,
    batchUpdates: boolean
  ): RepositoryOperation;
  createTag(
    path: string,
    name: string,
    target: string,
    force: boolean,
    annotated: boolean,
    message: string
  ): RepositoryOperation;
  deleteTags(path: string, names: string[]): RepositoryOperation;
  readConfig(
    path: string,
    scope: string,
    includes: boolean,
    explicitFile?: string
  ): RepositoryConfigEntry[];
  setConfig(
    path: string,
    key: string,
    value: string,
    scope: string,
    append: boolean,
    explicitFile?: string
  ): RepositoryOperation;
  unsetConfig(
    path: string,
    key: string,
    scope: string,
    all: boolean,
    explicitFile?: string
  ): RepositoryOperation;
  setCommandConfig(entries: RepositoryConfigEntry[]): void;
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
  listRemoteReferences(
    url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    patterns: string[],
    credentials?: RepositoryCredentials
  ): RepositoryRemoteAdvertisement;
  listRemotePushReferences(
    url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    patterns: string[],
    credentials?: RepositoryCredentials
  ): RepositoryRemoteAdvertisement;
  buildRemoteUploadPackUrl(url: string): string;
  buildRemoteReceivePackUrl(url: string): string;
  buildUploadPackRequest(
    wants: string[],
    haves: string[],
    capabilities: string[]
  ): ArrayBuffer;
  buildReceivePackRequest(
    path: string,
    updates: RepositoryRemotePushUpdate[],
    haves: string[],
    capabilities: string[]
  ): ArrayBuffer;
  parseUploadPackResponse(
    payload: ArrayBuffer
  ): RepositoryRemotePackResponse;
  parseReceivePackResponse(
    payload: ArrayBuffer,
    expectedReferences: string[]
  ): RepositoryRemotePushResult;
  installRemotePack(
    path: string,
    remoteName: string,
    packData: ArrayBuffer,
    references: RepositoryRemoteReference[],
    headTarget: string
  ): RepositoryOperation;
  fetch(
    path: string,
    remoteName: string,
    credentials?: RepositoryCredentials
  ): Promise<RepositoryOperation>;
  push(
    path: string,
    remoteName: string,
    updates: RepositoryRemotePushUpdate[],
    force: boolean,
    credentials?: RepositoryCredentials
  ): Promise<RepositoryOperation>;
  reflog(
    path: string,
    ref: string,
    maxCount: number,
    skip?: number,
    since?: string,
    until?: string
  ): RepositoryReflogEntry[];
  listReflogs(path: string): string[];
  reflogExists(path: string, ref: string): boolean;
  writeReflog(
    path: string,
    ref: string,
    oldObjectId: string,
    newObjectId: string,
    message: string
  ): RepositoryOperation;
  deleteReflogEntries(
    path: string,
    selectors: string[],
    rewrite: boolean,
    updateRef: boolean,
    dryRun: boolean,
    verbose: boolean
  ): RepositoryOperation;
  dropReflogs(
    path: string,
    refs: string[],
    all: boolean,
    singleWorktree: boolean
  ): RepositoryOperation;
  expireReflogs(
    path: string,
    refs: string[],
    expire: string,
    expireUnreachable: string,
    rewrite: boolean,
    updateRef: boolean,
    staleFix: boolean,
    dryRun: boolean,
    verbose: boolean,
    all: boolean,
    singleWorktree: boolean
  ): RepositoryOperation;
}
