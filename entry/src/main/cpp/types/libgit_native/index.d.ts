export interface NativeFileStatus {
  path: string;
  indexState: string;
  workTreeState: string;
  tracked: boolean;
  staged: boolean;
}

export interface NativeRemote {
  name: string;
  fetchUrl: string;
  pushUrl: string;
}

export interface NativeRemoteReference {
  objectId: string;
  name: string;
}

export interface NativeRemoteAdvertisement {
  success: boolean;
  responseCode: number;
  headTarget: string;
  references: NativeRemoteReference[];
  error: string;
}

export interface NativeRepositorySnapshot {
  valid: boolean;
  repositoryPath: string;
  gitDirectory: string;
  branch: string;
  head: string;
  detached: boolean;
  indexVersion: number;
  files: NativeFileStatus[];
  branches: string[];
  remotes: NativeRemote[];
  error: string;
}

export interface NativeRepositoryOperation {
  success: boolean;
  changedCount: number;
  snapshot: NativeRepositorySnapshot;
  output: string[];
  error: string;
}

export interface NativeCleanResult {
  success: boolean;
  changedCount: number;
  cleanedPaths: string[];
  skippedRepositories: string[];
  error: string;
}

export interface NativeCommit {
  id: string;
  subject: string;
  author: string;
  timestamp: string;
}

export interface NativeConfigEntry {
  key: string;
  value: string;
}

export interface NativeReflogEntry {
  oldId: string;
  newId: string;
  actor: string;
  timestamp: string;
  message: string;
}

export const inspectRepository:
  (path: string) => NativeRepositorySnapshot;
export const initializeRepository:
  (path: string, seedDemoFiles?: boolean) => NativeRepositorySnapshot;
export const directoryExists: (path: string) => boolean;
export const listDirectory: (path: string) => string[];
export const readWorkspaceFile:
  (path: string, filePath: string) => string;
export const writeWorkspaceFile:
  (
    path: string,
    filePath: string,
    content: string,
    append?: boolean
  ) => NativeRepositoryOperation;
export const cleanRepository:
  (
    path: string,
    dryRun?: boolean,
    directories?: boolean,
    quiet?: boolean,
    removeIgnored?: boolean,
    ignoredOnly?: boolean,
    force?: number,
    excludes?: string[],
    paths?: string[]
  ) => NativeCleanResult;
export const stageRepository:
  (path: string, paths: string[]) => NativeRepositoryOperation;
export const removeRepositoryPaths:
  (
    path: string,
    paths: string[],
    cached?: boolean,
    force?: boolean,
    recursive?: boolean
  ) => NativeRepositoryOperation;
export const moveRepositoryPath:
  (
    path: string,
    source: string,
    destination: string,
    force?: boolean
  ) => NativeRepositoryOperation;
export const restoreStaged:
  (path: string, paths: string[]) => NativeRepositoryOperation;
export const restoreWorkingTree:
  (path: string, paths: string[]) => NativeRepositoryOperation;
export const restoreFromSource:
  (
    path: string,
    source: string,
    paths: string[],
    staged?: boolean,
    worktree?: boolean
  ) => NativeRepositoryOperation;
export const resetHard:
  (path: string) => NativeRepositoryOperation;
export const commitRepository:
  (path: string, message: string) => NativeRepositoryOperation;
export const createBranch:
  (path: string, name: string, checkout?: boolean) => NativeRepositoryOperation;
export const moveBranch:
  (
    path: string,
    oldName: string,
    newName: string,
    force?: boolean
  ) => NativeRepositoryOperation;
export const copyBranch:
  (
    path: string,
    oldName: string,
    newName: string,
    force?: boolean
  ) => NativeRepositoryOperation;
export const switchBranch:
  (path: string, name: string) => NativeRepositoryOperation;
export const checkoutBranch:
  (
    path: string,
    name: string,
    startPoint?: string
  ) => NativeRepositoryOperation;
export const deleteBranch:
  (path: string, name: string, force?: boolean) => NativeRepositoryOperation;
export const diffRepository:
  (path: string, staged?: boolean) => string;
export const readLog:
  (path: string, maxCount?: number) => NativeCommit[];
export const showRevision:
  (
    path: string,
    revision?: string,
    statOnly?: boolean,
    oneLine?: boolean,
    paths?: string[]
  ) => string;
export const readTags:
  (path: string, patterns?: string[]) => string[];
export const readFiles:
  (
    path: string,
    cached?: boolean,
    modified?: boolean,
    deleted?: boolean,
    others?: boolean,
    ignored?: boolean,
    excludeStandard?: boolean,
    stage?: boolean,
    fullName?: boolean,
    paths?: string[]
  ) => string[];
export const hashFiles:
  (
    path: string,
    paths: string[],
    type?: string,
    write?: boolean
  ) => string[];
export const hashInput:
  (
    path: string,
    input: string,
    type?: string,
    write?: boolean
  ) => string;
export const checkIgnored:
  (
    path: string,
    paths: string[],
    noIndex?: boolean,
    verbose?: boolean
  ) => string[];
export const readObjectContent:
  (
    path: string,
    objectName: string,
    mode: string
  ) => string;
export const readTree:
  (
    path: string,
    treeish: string,
    recursive?: boolean,
    directoriesOnly?: boolean,
    includeTrees?: boolean,
    nameOnly?: boolean,
    objectOnly?: boolean,
    longFormat?: boolean,
    fullName?: boolean,
    fullTree?: boolean,
    paths?: string[]
  ) => string[];
export const readReferences:
  (
    path: string,
    heads?: boolean,
    tags?: boolean,
    includeHead?: boolean,
    dereference?: boolean,
    verify?: boolean,
    quiet?: boolean,
    hashOnly?: boolean,
    abbreviation?: number,
    patterns?: string[]
  ) => string[];
export const excludeExistingReferences:
  (
    path: string,
    input: string,
    pattern?: string
  ) => string[];
export const readRevisionList:
  (
    path: string,
    all?: boolean,
    branches?: boolean,
    tags?: boolean,
    remotes?: boolean,
    parents?: boolean,
    count?: boolean,
    reverse?: boolean,
    firstParent?: boolean,
    noMerges?: boolean,
    merges?: boolean,
    abbreviate?: boolean,
    abbreviation?: number,
    maxCount?: number,
    revisions?: string[],
    paths?: string[]
  ) => string[];
export const readMergeBases:
  (
    path: string,
    all?: boolean,
    octopus?: boolean,
    independent?: boolean,
    revisions?: string[]
  ) => string[];
export const isAncestor:
  (path: string, ancestor: string, descendant: string) => boolean;
export const findForkPoint:
  (path: string, reference: string, derived: string) => string;
export const formatReferences:
  (
    path: string,
    count?: number,
    format?: string,
    sortKeys?: string[],
    patterns?: string[],
    excludes?: string[],
    pointsAt?: string,
    merged?: string,
    noMerged?: string,
    contains?: string,
    noContains?: string,
    ignoreCase?: boolean,
    includeRootRefs?: boolean
  ) => string[];
export const readSymbolicReference:
  (
    path: string,
    name: string,
    shortName?: boolean,
    recurse?: boolean
  ) => string;
export const updateSymbolicReference:
  (
    path: string,
    name: string,
    target?: string,
    deleteReference?: boolean,
    message?: string
  ) => NativeRepositoryOperation;
export const updateReference:
  (
    path: string,
    name: string,
    newValue?: string,
    oldValue?: string,
    deleteReference?: boolean,
    noDeref?: boolean,
    message?: string,
    createReflog?: boolean
  ) => NativeRepositoryOperation;
export const updateReferences:
  (
    path: string,
    input: string,
    noDeref?: boolean,
    createReflog?: boolean,
    message?: string,
    nullTerminated?: boolean,
    batchUpdates?: boolean
  ) => NativeRepositoryOperation;
export const createTag:
  (
    path: string,
    name: string,
    target?: string,
    force?: boolean,
    annotated?: boolean,
    message?: string
  ) => NativeRepositoryOperation;
export const deleteTags:
  (path: string, names: string[]) => NativeRepositoryOperation;
export const readConfig:
  (path: string) => NativeConfigEntry[];
export const setConfigValue:
  (path: string, key: string, value: string) => NativeRepositoryOperation;
export const unsetConfigValue:
  (path: string, key: string) => NativeRepositoryOperation;
export const addRemote:
  (path: string, name: string, url: string) => NativeRepositoryOperation;
export const removeRemote:
  (path: string, name: string) => NativeRepositoryOperation;
export const renameRemote:
  (
    path: string,
    oldName: string,
    newName: string
  ) => NativeRepositoryOperation;
export const getRemoteUrl:
  (path: string, name: string, push?: boolean) => string;
export const setRemoteUrl:
  (
    path: string,
    name: string,
    url: string,
    push?: boolean
  ) => NativeRepositoryOperation;
export const listRemoteReferences:
  (
    url: string,
    heads?: boolean,
    tags?: boolean,
    refsOnly?: boolean,
    patterns?: string[]
  ) => NativeRemoteAdvertisement;
export const readReflog:
  (path: string, ref?: string, maxCount?: number) => NativeReflogEntry[];
