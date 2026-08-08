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

export const inspectRepository:
  (path: string) => NativeRepositorySnapshot;
export const initializeRepository:
  (path: string, seedDemoFiles?: boolean) => NativeRepositorySnapshot;
export const directoryExists: (path: string) => boolean;
export const listDirectory: (path: string) => string[];
