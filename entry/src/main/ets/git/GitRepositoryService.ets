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

export interface GitRepositoryService {
  inspect(path: string): RepositorySnapshot;
  initialize(path: string, seedDemoFiles: boolean): RepositorySnapshot;
  directoryExists(path: string): boolean;
  listDirectory(path: string): string[];
}
