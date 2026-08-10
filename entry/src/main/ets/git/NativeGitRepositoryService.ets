import { http } from '@kit.NetworkKit';
import {
  addRemote,
  buildRemoteReceivePackUrl,
  buildRemoteUploadPackUrl,
  buildReceivePackRequest,
  buildUploadPackRequest,
  checkoutBranch,
  checkIgnored,
  cleanRepository,
  copyBranch,
  commitRepository,
  createBranch,
  createTag,
  deleteReflogEntries,
  deleteBranch,
  deleteTags,
  directoryExists,
  diffRepository,
  dropReflogs,
  expireReflogs,
  excludeExistingReferences,
  findForkPoint,
  formatReferences,
  getRemoteUrl,
  hashFiles,
  hashInput,
  initializeRepository,
  installRemotePack,
  inspectRepository,
  isAncestor,
  listDirectory,
  listReflogs,
  listRemoteReferences,
  listRemotePushReferences,
  readWorkspaceFile,
  moveBranch,
  moveRepositoryPath,
  NativeCommit,
  NativeConfigEntry,
  NativeFileStatus,
  NativeRemote,
  NativeRemoteAdvertisement,
  NativeRemotePackResponse,
  NativeRemotePushResult,
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
  reflogExists,
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
  setCommandConfig,
  setRemoteUrl,
  showRevision,
  stageRepository,
  switchBranch,
  unsetConfigValue,
  updateReference,
  updateReferences,
  updateSymbolicReference,
  parseUploadPackResponse,
  parseReceivePackResponse,
  writeReflog,
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
  RepositoryRemotePackResponse,
  RepositoryRemoteReference,
  RepositoryRemotePushResult,
  RepositoryRemotePushUpdate,
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

  readConfig(
    path: string,
    scope: string,
    includes: boolean,
    explicitFile: string = ''
  ): RepositoryConfigEntry[] {
    return readConfig(path, scope, includes, explicitFile).map(
      (entry: NativeConfigEntry): RepositoryConfigEntry => ({
        key: entry.key,
        value: entry.value
      })
    );
  }

  setConfig(
    path: string,
    key: string,
    value: string,
    scope: string,
    append: boolean,
    explicitFile: string = ''
  ): RepositoryOperation {
    return this.toOperation(
      setConfigValue(path, key, value, scope, append, explicitFile)
    );
  }

  unsetConfig(
    path: string,
    key: string,
    scope: string,
    all: boolean,
    explicitFile: string = ''
  ): RepositoryOperation {
    return this.toOperation(
      unsetConfigValue(path, key, scope, all, explicitFile)
    );
  }

  setCommandConfig(entries: RepositoryConfigEntry[]): void {
    setCommandConfig(entries.map(
      (entry: RepositoryConfigEntry): string =>
        `${entry.key}=${entry.value}`
    ));
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
      capabilities: advertisement.capabilities.slice(),
      error: advertisement.error
    };
  }

  listRemotePushReferences(
    url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    patterns: string[]
  ): RepositoryRemoteAdvertisement {
    const advertisement: NativeRemoteAdvertisement =
      listRemotePushReferences(
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
      capabilities: advertisement.capabilities.slice(),
      error: advertisement.error
    };
  }

  buildRemoteUploadPackUrl(url: string): string {
    return buildRemoteUploadPackUrl(url);
  }

  buildRemoteReceivePackUrl(url: string): string {
    return buildRemoteReceivePackUrl(url);
  }

  buildUploadPackRequest(
    wants: string[],
    haves: string[],
    capabilities: string[]
  ): ArrayBuffer {
    return buildUploadPackRequest(wants, haves, capabilities);
  }

  buildReceivePackRequest(
    path: string,
    updates: RepositoryRemotePushUpdate[],
    haves: string[],
    capabilities: string[]
  ): ArrayBuffer {
    return buildReceivePackRequest(
      path,
      updates.map(
        (update: RepositoryRemotePushUpdate): string =>
          `${update.oldObjectId} ${update.newObjectId} ${update.name}`
      ),
      haves,
      capabilities
    );
  }

  parseUploadPackResponse(
    payload: ArrayBuffer
  ): RepositoryRemotePackResponse {
    const response: NativeRemotePackResponse =
      parseUploadPackResponse(payload);
    return {
      success: response.success,
      acknowledged: response.acknowledged,
      objectCount: response.objectCount,
      packData: response.packData,
      progress: response.progress,
      error: response.error
    };
  }

  parseReceivePackResponse(
    payload: ArrayBuffer,
    expectedReferences: string[]
  ): RepositoryRemotePushResult {
    const response: NativeRemotePushResult =
      parseReceivePackResponse(payload, expectedReferences);
    return {
      success: response.success,
      unpacked: response.unpacked,
      output: response.output.slice(),
      error: response.error
    };
  }

  installRemotePack(
    path: string,
    remoteName: string,
    packData: ArrayBuffer,
    references: RepositoryRemoteReference[],
    headTarget: string
  ): RepositoryOperation {
    return this.toOperation(
      installRemotePack(
        path,
        remoteName,
        packData,
        references.map(
          (reference: RepositoryRemoteReference): string =>
            reference.name
        ),
        references.map(
          (reference: RepositoryRemoteReference): string =>
            reference.objectId
        ),
        headTarget
      )
    );
  }

  async fetch(
    path: string,
    remoteName: string
  ): Promise<RepositoryOperation> {
    let remoteUrl: string = '';
    try {
      remoteUrl = this.getRemoteUrl(path, remoteName, false);
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    }

    const advertisement: RepositoryRemoteAdvertisement =
      this.listRemoteReferences(
        remoteUrl,
        true,
        false,
        true,
        []
      );
    if (!advertisement.success) {
      return this.failedOperation(path, advertisement.error);
    }
    const references: RepositoryRemoteReference[] =
      advertisement.references.filter(
        (reference: RepositoryRemoteReference): boolean =>
          reference.name.startsWith('refs/heads/')
      );
    const wants: string[] = [];
    for (const reference of references) {
      if (!wants.includes(reference.objectId)) {
        wants.push(reference.objectId);
      }
    }
    if (wants.length === 0) {
      return this.failedOperation(
        path,
        'Remote did not advertise any branch references.'
      );
    }

    const snapshot: RepositorySnapshot = this.inspect(path);
    const haves: string[] =
      snapshot.valid && snapshot.head.length > 0
        ? [snapshot.head]
        : [];
    let requestData: ArrayBuffer;
    let uploadUrl: string;
    try {
      requestData = this.buildUploadPackRequest(
        wants,
        haves,
        advertisement.capabilities
      );
      uploadUrl = this.buildRemoteUploadPackUrl(remoteUrl);
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    }

    const request: http.HttpRequest = http.createHttp();
    try {
      const response: http.HttpResponse =
        await request.request(uploadUrl, {
          method: http.RequestMethod.POST,
          extraData: requestData,
          expectDataType: http.HttpDataType.ARRAY_BUFFER,
          usingCache: false,
          readTimeout: 120000,
          connectTimeout: 30000,
          header: {
            'Content-Type':
              'application/x-git-upload-pack-request',
            'Accept':
              'application/x-git-upload-pack-result',
            'User-Agent': 'Harmony-Git-Bash/0.1'
          }
        });
      if (response.responseCode !== 200) {
        return this.failedOperation(
          path,
          `Remote server returned HTTP ${response.responseCode}.`
        );
      }
      if (!(response.result instanceof ArrayBuffer)) {
        return this.failedOperation(
          path,
          'Remote upload-pack response is not binary data.'
        );
      }
      const packResponse: RepositoryRemotePackResponse =
        this.parseUploadPackResponse(response.result);
      if (!packResponse.success) {
        return this.failedOperation(path, packResponse.error);
      }
      const installed: RepositoryOperation =
        this.installRemotePack(
          path,
          remoteName,
          packResponse.packData,
          references,
          advertisement.headTarget
        );
      if (installed.success &&
          packResponse.progress.trim().length > 0) {
        installed.output.unshift(packResponse.progress.trim());
      }
      return installed;
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    } finally {
      request.destroy();
    }
  }

  async push(
    path: string,
    remoteName: string,
    updates: RepositoryRemotePushUpdate[],
    force: boolean
  ): Promise<RepositoryOperation> {
    let remoteUrl: string = '';
    try {
      remoteUrl = this.getRemoteUrl(path, remoteName, true);
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    }

    const advertisement: RepositoryRemoteAdvertisement =
      this.listRemotePushReferences(
        remoteUrl,
        true,
        true,
        true,
        []
      );
    if (!advertisement.success) {
      return this.failedOperation(path, advertisement.error);
    }
    const supportsReportStatus: boolean =
      advertisement.capabilities.some(
        (capability: string): boolean =>
          capability === 'report-status' ||
          capability === 'report-status-v2'
      );
    if (!supportsReportStatus) {
      return this.failedOperation(
        path,
        'Remote receive-pack does not advertise report-status support.'
      );
    }

    const zeroObjectId: string = '0'.repeat(40);
    const resolvedUpdates: RepositoryRemotePushUpdate[] = [];
    const haves: string[] = [];
    for (const reference of advertisement.references) {
      if (!haves.includes(reference.objectId)) {
        haves.push(reference.objectId);
      }
    }
    for (const update of updates) {
      const remoteReference: RepositoryRemoteReference | undefined =
        advertisement.references.find(
          (reference: RepositoryRemoteReference): boolean =>
            reference.name === update.name
        );
      const oldObjectId: string =
        update.oldObjectId.length > 0
          ? update.oldObjectId
          : remoteReference?.objectId ?? zeroObjectId;
      const newObjectId: string =
        update.newObjectId.length > 0
          ? update.newObjectId
          : zeroObjectId;
      update.oldObjectId = oldObjectId;
      resolvedUpdates.push({
        oldObjectId,
        newObjectId,
        name: update.name
      });
    }
    if (!force) {
      for (const update of resolvedUpdates) {
        if (update.oldObjectId === zeroObjectId ||
            update.newObjectId === zeroObjectId ||
            update.oldObjectId === update.newObjectId) {
          continue;
        }
        try {
          if (!this.isAncestor(
            path,
            update.oldObjectId,
            update.newObjectId
          )) {
            return this.failedOperation(
              path,
              `non-fast-forward update rejected for ${update.name}; ` +
              'fetch first or use --force.'
            );
          }
        } catch (_error) {
          return this.failedOperation(
            path,
            `cannot verify a fast-forward update for ${update.name}; ` +
            'fetch first or use --force.'
          );
        }
      }
    }

    let requestData: ArrayBuffer;
    let receiveUrl: string;
    try {
      requestData = this.buildReceivePackRequest(
        path,
        resolvedUpdates,
        haves,
        advertisement.capabilities
      );
      receiveUrl = this.buildRemoteReceivePackUrl(remoteUrl);
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    }

    const request: http.HttpRequest = http.createHttp();
    try {
      const response: http.HttpResponse =
        await request.request(receiveUrl, {
          method: http.RequestMethod.POST,
          extraData: requestData,
          expectDataType: http.HttpDataType.ARRAY_BUFFER,
          usingCache: false,
          readTimeout: 120000,
          connectTimeout: 30000,
          header: {
            'Content-Type':
              'application/x-git-receive-pack-request',
            'Accept':
              'application/x-git-receive-pack-result',
            'User-Agent': 'Harmony-Git-Bash/0.1'
          }
        });
      if (response.responseCode !== 200) {
        return this.failedOperation(
          path,
          `Remote server returned HTTP ${response.responseCode}.`
        );
      }
      if (!(response.result instanceof ArrayBuffer)) {
        return this.failedOperation(
          path,
          'Remote receive-pack response is not binary data.'
        );
      }
      const parsed: RepositoryRemotePushResult =
        this.parseReceivePackResponse(
          response.result,
          resolvedUpdates.map(
            (update: RepositoryRemotePushUpdate): string =>
              update.name
          )
        );
      if (!parsed.success) {
        return this.failedOperation(path, parsed.error);
      }
      const snapshot: RepositorySnapshot = this.inspect(path);
      return {
        success: true,
        changedCount: resolvedUpdates.length,
        snapshot,
        output: parsed.output.slice(),
        error: ''
      };
    } catch (error) {
      return this.failedOperation(path, this.errorMessage(error));
    } finally {
      request.destroy();
    }
  }

  reflog(
    path: string,
    ref: string,
    maxCount: number,
    skip: number = 0,
    since: string = '',
    until: string = ''
  ): RepositoryReflogEntry[] {
    return readReflog(path, ref, maxCount, skip, since, until).map(
      (entry: NativeReflogEntry): RepositoryReflogEntry => ({
        oldId: entry.oldId,
        newId: entry.newId,
        actor: entry.actor,
        timestamp: entry.timestamp,
        message: entry.message,
        index: entry.index,
        selector: entry.selector,
        subject: entry.subject,
        author: entry.author,
        commitTimestamp: entry.commitTimestamp
      })
    );
  }

  listReflogs(path: string): string[] {
    return listReflogs(path);
  }

  reflogExists(path: string, ref: string): boolean {
    return reflogExists(path, ref);
  }

  writeReflog(
    path: string,
    ref: string,
    oldObjectId: string,
    newObjectId: string,
    message: string
  ): RepositoryOperation {
    return writeReflog(
      path,
      ref,
      oldObjectId,
      newObjectId,
      message
    );
  }

  deleteReflogEntries(
    path: string,
    selectors: string[],
    rewrite: boolean,
    updateRef: boolean,
    dryRun: boolean,
    verbose: boolean
  ): RepositoryOperation {
    return deleteReflogEntries(
      path,
      selectors,
      rewrite,
      updateRef,
      dryRun,
      verbose
    );
  }

  dropReflogs(
    path: string,
    refs: string[],
    all: boolean,
    singleWorktree: boolean
  ): RepositoryOperation {
    return dropReflogs(path, refs, all, singleWorktree);
  }

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
  ): RepositoryOperation {
    return expireReflogs(
      path,
      refs,
      expire,
      expireUnreachable,
      rewrite,
      updateRef,
      staleFix,
      dryRun,
      verbose,
      all,
      singleWorktree
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

  private failedOperation(
    path: string,
    error: string
  ): RepositoryOperation {
    const snapshot: RepositorySnapshot = this.inspect(path);
    return {
      success: false,
      changedCount: 0,
      snapshot,
      output: [],
      error
    };
  }

  private errorMessage(error: Object): string {
    return error instanceof Error ? error.message : `${error}`;
  }
}
