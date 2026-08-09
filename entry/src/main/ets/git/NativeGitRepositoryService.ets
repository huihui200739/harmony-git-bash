import {
  addRemote,
  checkoutBranch,
  checkIgnored,
  cleanRepository,
  copyBranch,
  commitRepository,
  createBranch,
  createTag,
  deleteBranch,
  deleteTags,
  directoryExists,
  diffRepository,
  excludeExistingReferences,
  findForkPoint,
  formatReferences,
  getRemoteUrl,
  hashFiles,
  hashInput,
  initializeRepository,
  inspectRepository,
  isAncestor,
  listDirectory,
  listRemoteReferences,
  readWorkspaceFile,
  moveBranch,
  moveRepositoryPath,
  NativeCommit,
  NativeConfigEntry,
  NativeFileStatus,
  NativeRemote,
  NativeRemoteAdvertisement,
  NativeRemoteReference,
  NativeReflogEntry,
  NativeRepositoryOperation,
  NativeRepositorySnapshot,
  readConfig,
  readFiles,
  readLog,
  readMergeBases,
  readObjectContent,
  readReferences,
  readRevisionList,
  readReflog,
  readSymbolicReference,
  readTags,
  readTree,
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
  unsetConfigValue,
  updateReference,
  updateReferences,
  updateSymbolicReference,
  writeWorkspaceFile
} from 'libgit_native.so';
import {
  GitRepositoryService,
  RepositoryConfigEntry,
  RepositoryCommit,
  RepositoryCleanOptions,
  RepositoryCleanResult,
  RepositoryFileStatus,
  RepositoryForEachRefOptions,
  RepositoryListFilesOptions,
  RepositoryListTreeOptions,
  RepositoryMergeBaseOptions,
  RepositoryOperation,
  RepositoryReflogEntry,
  RepositoryRemote,
  RepositoryRemoteAdvertisement,
  RepositoryRemoteReference,
  RepositoryRevListOptions,
  RepositoryShowRefOptions,
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

  readFile(path: string, filePath: string): string {
    return readWorkspaceFile(path, filePath);
  }

  writeFile(
    path: string,
    filePath: string,
    content: string,
    append: boolean
  ): RepositoryOperation {
    return this.toOperation(
      writeWorkspaceFile(path, filePath, content, append)
    );
  }

  clean(
    path: string,
    options: RepositoryCleanOptions
  ): RepositoryCleanResult {
    return cleanRepository(
      path,
      options.dryRun,
      options.directories,
      options.quiet,
      options.removeIgnored,
      options.ignoredOnly,
      options.force,
      options.excludes,
      options.paths
    );
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
    oneLine: boolean,
    paths: string[]
  ): string {
    return showRevision(path, revision, statOnly, oneLine, paths);
  }

  tags(path: string, patterns: string[]): string[] {
    return readTags(path, patterns);
  }

  listFiles(
    path: string,
    options: RepositoryListFilesOptions
  ): string[] {
    return readFiles(
      path,
      options.cached,
      options.modified,
      options.deleted,
      options.others,
      options.ignored,
      options.excludeStandard,
      options.stage,
      options.fullName,
      options.paths
    );
  }

  hashObject(
    path: string,
    paths: string[],
    type: string,
    write: boolean
  ): string[] {
    return hashFiles(path, paths, type, write);
  }

  hashInput(
    path: string,
    input: string,
    type: string,
    write: boolean
  ): string {
    return hashInput(path, input, type, write);
  }

  checkIgnore(
    path: string,
    paths: string[],
    noIndex: boolean,
    verbose: boolean
  ): string[] {
    return checkIgnored(path, paths, noIndex, verbose);
  }

  catFile(
    path: string,
    objectName: string,
    mode: string
  ): string {
    return readObjectContent(path, objectName, mode);
  }

  listTree(
    path: string,
    treeish: string,
    options: RepositoryListTreeOptions
  ): string[] {
    return readTree(
      path,
      treeish,
      options.recursive,
      options.directoriesOnly,
      options.includeTrees,
      options.nameOnly,
      options.objectOnly,
      options.longFormat,
      options.fullName,
      options.fullTree,
      options.paths
    );
  }

  showRef(
    path: string,
    options: RepositoryShowRefOptions
  ): string[] {
    return readReferences(
      path,
      options.heads,
      options.tags,
      options.includeHead,
      options.dereference,
      options.verify,
      options.quiet,
      options.hashOnly,
      options.abbreviation,
      options.patterns
    );
  }

  excludeExistingRefs(
    path: string,
    input: string,
    pattern: string
  ): string[] {
    return excludeExistingReferences(path, input, pattern);
  }

  revList(
    path: string,
    options: RepositoryRevListOptions
  ): string[] {
    return readRevisionList(
      path,
      options.all,
      options.branches,
      options.tags,
      options.remotes,
      options.parents,
      options.count,
      options.reverse,
      options.firstParent,
      options.noMerges,
      options.merges,
      options.abbreviate,
      options.abbreviation,
      options.maxCount,
      options.revisions,
      options.paths
    );
  }

  mergeBase(
    path: string,
    options: RepositoryMergeBaseOptions
  ): string[] {
    return readMergeBases(
      path,
      options.all,
      options.octopus,
      options.independent,
      options.revisions
    );
  }

  isAncestor(
    path: string,
    ancestor: string,
    descendant: string
  ): boolean {
    return isAncestor(path, ancestor, descendant);
  }

  findForkPoint(
    path: string,
    reference: string,
    derived: string
  ): string {
    return findForkPoint(path, reference, derived);
  }

  forEachRef(
    path: string,
    options: RepositoryForEachRefOptions
  ): string[] {
    return formatReferences(
      path,
      options.count,
      options.format,
      options.sortKeys,
      options.patterns,
      options.excludes,
      options.pointsAt,
      options.merged,
      options.noMerged,
      options.contains,
      options.noContains,
      options.ignoreCase,
      options.includeRootRefs
    );
  }

  symbolicRef(
    path: string,
    name: string,
    shortName: boolean,
    recurse: boolean
  ): string {
    return readSymbolicReference(path, name, shortName, recurse);
  }

  updateSymbolicRef(
    path: string,
    name: string,
    target: string,
    deleteReference: boolean,
    message: string
  ): RepositoryOperation {
    return this.toOperation(
      updateSymbolicReference(
        path,
        name,
        target,
        deleteReference,
        message
      )
    );
  }

  updateRef(
    path: string,
    name: string,
    newValue: string,
    oldValue: string,
    deleteReference: boolean,
    noDeref: boolean,
    message: string,
    createReflog: boolean
  ): RepositoryOperation {
    return this.toOperation(
      updateReference(
        path,
        name,
        newValue,
        oldValue,
        deleteReference,
        noDeref,
        message,
        createReflog
      )
    );
  }

  updateRefs(
    path: string,
    input: string,
    noDeref: boolean,
    createReflog: boolean,
    message: string,
    nullTerminated: boolean,
    batchUpdates: boolean
  ): RepositoryOperation {
    return this.toOperation(
      updateReferences(
        path,
        input,
        noDeref,
        createReflog,
        message,
        nullTerminated,
        batchUpdates
      )
    );
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

  listRemoteReferences(
    url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    patterns: string[]
  ): RepositoryRemoteAdvertisement {
    const advertisement: NativeRemoteAdvertisement =
      listRemoteReferences(
        url,
        heads,
        tags,
        refsOnly,
        patterns
      );
    return {
      success: advertisement.success,
      responseCode: advertisement.responseCode,
      headTarget: advertisement.headTarget,
      references: advertisement.references.map(
        (reference: NativeRemoteReference):
          RepositoryRemoteReference => ({
            objectId: reference.objectId,
            name: reference.name
          })
      ),
      error: advertisement.error
    };
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
      output: nativeOperation.output.slice(),
      error: nativeOperation.error
    };
  }
}
