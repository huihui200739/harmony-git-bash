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
  error: string;
}

export interface NativeCommit {
  id: string;
  subject: string;
  author: string;
  timestamp: string;
}

export const inspectRepository:
  (path: string) => NativeRepositorySnapshot;
export const initializeRepository:
  (path: string, seedDemoFiles?: boolean) => NativeRepositorySnapshot;
export const directoryExists: (path: string) => boolean;
export const listDirectory: (path: string) => string[];
export const stageRepository:
  (path: string, paths: string[]) => NativeRepositoryOperation;
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
