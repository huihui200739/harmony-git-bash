import { describe, expect, it } from '@ohos/hypium';
import {
  GitRepositoryService,
  RepositoryCleanOptions,
  RepositoryCleanResult,
  RepositoryConfigEntry,
  RepositoryCommit,
  RepositoryForEachRefOptions,
  RepositoryListFilesOptions,
  RepositoryListTreeOptions,
  RepositoryMergeBaseOptions,
  RepositoryOperation,
  RepositoryReflogEntry,
  RepositoryRemoteAdvertisement,
  RepositoryRemotePackResponse,
  RepositoryRemoteReference,
  RepositoryRemotePushResult,
  RepositoryRemotePushUpdate,
  RepositoryRevListOptions,
  RepositoryShowRefOptions,
  RepositorySnapshot
} from '../main/ets/git/GitRepositoryService';
import { GitShell } from '../main/ets/git/GitShell';

class MockGitRepositoryService implements GitRepositoryService {
  private readonly demoPath: string = '/sandbox/workspace/demo-app';
  private readonly externalPath: string = '/external/repository';
  private readonly shellFiles: Record<string, string> = {};
  branchUpstreamConfigured: boolean = false;
  currentHeadObjectId: string =
    '1234567890abcdef1234567890abcdef12345678';
  remoteObjectId: string =
    'abcdef1234567890abcdef1234567890abcdef12';
  fastForwardAllowed: boolean = true;
  lastInitializePath: string = '';
  lastRemotePath: string = '';
  lastRemoteName: string = '';
  lastRemoteUrl: string = '';
  lastFetchPath: string = '';
  lastFetchRemote: string = '';
  lastPushPath: string = '';
  lastPushRemote: string = '';
  lastPushUpdates: RepositoryRemotePushUpdate[] = [];
  lastPushForce: boolean = false;
  lastCheckoutPath: string = '';
  lastCheckoutName: string = '';
  lastCheckoutStartPoint: string = '';
  lastConfigKey: string = '';
  lastConfigValue: string = '';
  lastConfigReadScope: string = '';
  lastConfigReadIncludes: boolean = false;
  lastConfigReadFile: string = '';
  lastConfigScope: string = '';
  lastConfigAppend: boolean = false;
  lastConfigWriteFile: string = '';
  lastUnsetConfigScope: string = '';
  lastUnsetConfigAll: boolean = false;
  lastUnsetConfigFile: string = '';
  multivarConfig: boolean = false;
  typedConfig: boolean = false;
  readonly configWrites: RepositoryConfigEntry[] = [];
  commandConfigEntries: RepositoryConfigEntry[] = [];
  commandConfigApplicationCount: number = 0;
  commandConfigClearCount: number = 0;
  lastUpdateRefPath: string = '';
  lastUpdateRefName: string = '';
  lastUpdateRefValue: string = '';
  lastSymbolicRefPath: string = '';
  lastSymbolicRefName: string = '';
  lastSymbolicRefTarget: string = '';
  lastCleanPath: string = '';
  lastCleanOptions: RepositoryCleanOptions = {
    dryRun: false,
    directories: false,
    quiet: false,
    removeIgnored: false,
    ignoredOnly: false,
    force: 0,
    excludes: [],
    paths: []
  };
  lastReflogWriteRef: string = '';
  lastReflogWriteOldObjectId: string = '';
  lastReflogWriteNewObjectId: string = '';
  lastReflogWriteMessage: string = '';
  lastReflogRef: string = '';
  lastReflogMaxCount: number = 0;
  lastReflogSkip: number = 0;
  lastReflogSince: string = '';
  lastReflogUntil: string = '';
  lastReflogDeleteSelectors: string[] = [];
  lastReflogDeleteRewrite: boolean = false;
  lastReflogDeleteUpdateRef: boolean = false;
  lastReflogDeleteDryRun: boolean = false;
  lastReflogDeleteVerbose: boolean = false;
  lastReflogDropRefs: string[] = [];
  lastReflogDropAll: boolean = false;
  lastReflogDropSingleWorktree: boolean = false;
  lastReflogExpireRefs: string[] = [];
  lastReflogExpire: string = '';
  lastReflogExpireUnreachable: string = '';
  lastReflogExpireRewrite: boolean = false;
  lastReflogExpireUpdateRef: boolean = false;
  lastReflogExpireStaleFix: boolean = false;
  lastReflogExpireDryRun: boolean = false;
  lastReflogExpireVerbose: boolean = false;
  lastReflogExpireAll: boolean = false;
  lastReflogExpireSingleWorktree: boolean = false;
  lastTagName: string = '';
  lastTagTarget: string = '';
  lastTagForce: boolean = false;
  lastTagAnnotated: boolean = false;
  lastTagMessage: string = '';
  tagCreateCount: number = 0;

  inspect(path: string): RepositorySnapshot {
    if (path === this.demoPath || path.startsWith(`${this.demoPath}/`)) {
      return this.snapshot(this.demoPath);
    }
    if (path === this.externalPath ||
        path.startsWith(`${this.externalPath}/`)) {
      return this.snapshot(this.externalPath);
    }
    return this.invalid(path);
  }

  initialize(path: string, _seedDemoFiles: boolean): RepositorySnapshot {
    this.lastInitializePath = path;
    return this.snapshot(path);
  }

  directoryExists(path: string): boolean {
    return path === '/sandbox' ||
      path === '/sandbox/workspace' ||
      path === this.demoPath ||
      path === `${this.demoPath}/src` ||
      path === '/sandbox/empty' ||
      path === '/sandbox/existing' ||
      path === this.externalPath;
  }

  listDirectory(path: string): string[] {
    if (!this.directoryExists(path)) {
      throw new Error('No such directory');
    }
    if (path === '/sandbox/empty') {
      return [];
    }
    if (path === '/sandbox/existing') {
      return ['existing.txt'];
    }
    return ['.git/', 'README.md', 'src/'];
  }

  readFile(_path: string, filePath: string): string {
    if (this.shellFiles[filePath] === undefined) {
      throw new Error('No such file or directory');
    }
    return this.shellFiles[filePath];
  }

  writeFile(
    path: string,
    filePath: string,
    content: string,
    append: boolean
  ): RepositoryOperation {
    this.shellFiles[filePath] =
      (append ? this.shellFiles[filePath] ?? '' : '') + content;
    return this.operation(path, 1);
  }

  clean(
    path: string,
    options: RepositoryCleanOptions
  ): RepositoryCleanResult {
    this.lastCleanPath = path;
    this.lastCleanOptions = options;
    return {
      success: true,
      changedCount: 2,
      cleanedPaths: ['notes.txt', 'build/'],
      skippedRepositories: ['nested/'],
      error: ''
    };
  }

  stage(path: string, paths: string[]): RepositoryOperation {
    return this.operation(path, paths.length > 0 ? 1 : 0);
  }

  remove(
    path: string,
    _paths: string[],
    _cached: boolean,
    _force: boolean,
    _recursive: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  movePath(
    path: string,
    _source: string,
    _destination: string,
    _force: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  restoreStaged(path: string, _paths: string[]): RepositoryOperation {
    return this.operation(path, 1);
  }

  restoreWorkingTree(path: string, _paths: string[]): RepositoryOperation {
    return this.operation(path, 1);
  }

  restoreFromSource(
    path: string,
    _source: string,
    _paths: string[],
    _staged: boolean,
    _worktree: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  resetHard(path: string): RepositoryOperation {
    return this.operation(path, 1);
  }

  commit(path: string, _message: string): RepositoryOperation {
    return this.operation(path, 1);
  }

  createBranch(path: string, _name: string, _checkout: boolean): RepositoryOperation {
    return this.operation(path, 1);
  }

  moveBranch(
    path: string,
    _oldName: string,
    _newName: string,
    _force: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  copyBranch(
    path: string,
    _oldName: string,
    _newName: string,
    _force: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  switchBranch(path: string, _name: string): RepositoryOperation {
    return this.operation(path, 1);
  }

  checkoutBranch(
    path: string,
    name: string,
    startPoint: string
  ): RepositoryOperation {
    this.lastCheckoutPath = path;
    this.lastCheckoutName = name;
    this.lastCheckoutStartPoint = startPoint;
    return this.operation(path, 1);
  }

  deleteBranch(path: string, _name: string, _force: boolean): RepositoryOperation {
    return this.operation(path, 1);
  }

  diff(_path: string, _staged: boolean): string {
    return '';
  }

  log(_path: string, _maxCount: number): RepositoryCommit[] {
    return [];
  }

  show(
    _path: string,
    revision: string,
    statOnly: boolean,
    oneLine: boolean,
    paths: string[]
  ): string {
    const pathSuffix: string =
      paths.length === 0 ? '' : `|${paths.join(',')}`;
    return `${revision.length === 0 ? 'HEAD' : revision}|${statOnly}|${oneLine}${pathSuffix}`;
  }

  tags(_path: string, patterns: string[]): string[] {
    const tags: string[] = ['v1.0.0', 'v2.0.0'];
    if (patterns.length === 0) {
      return tags;
    }
    return tags.filter(
      (tag: string): boolean => patterns.some(
        (pattern: string): boolean => {
          if (pattern.endsWith('*')) {
            return tag.startsWith(pattern.slice(0, pattern.length - 1));
          }
          return tag === pattern;
        }
      )
    );
  }

  listFiles(
    _path: string,
    options: RepositoryListFilesOptions
  ): string[] {
    const files: string[] = [];
    const hasSelector: boolean =
      options.cached ||
      options.modified ||
      options.deleted ||
      options.others;
    if (options.cached || !hasSelector) {
      files.push('README.md', 'src/main.ets');
    }
    if (options.modified && !files.includes('README.md')) {
      files.push('README.md');
    }
    if (options.deleted) {
      files.push('deleted.txt');
    }
    if (options.others) {
      files.push(options.ignored ? 'ignored.log' : 'notes.txt');
    }
    const selected: string[] = files.filter(
      (file: string): boolean =>
        options.paths.length === 0 ||
        options.paths.some(
          (path: string): boolean =>
            file === path || file.startsWith(`${path}/`)
        )
    ).sort();
    return options.stage && !options.others
      ? selected.map(
          (file: string): string =>
            `100644 1234567890abcdef1234567890abcdef12345678 0\t${file}`
        )
      : selected;
  }

  hashObject(
    _path: string,
    paths: string[],
    type: string,
    write: boolean
  ): string[] {
    return paths.map(
      (path: string): string => `${type}|${write}|${path}`
    );
  }

  hashInput(
    _path: string,
    input: string,
    type: string,
    write: boolean
  ): string {
    return `${type}|${write}|${input}`;
  }

  checkIgnore(
    _path: string,
    paths: string[],
    noIndex: boolean,
    verbose: boolean
  ): string[] {
    return paths.filter(
      (path: string): boolean => path.endsWith('.log')
    ).map(
      (path: string): string => verbose
        ? `.gitignore:1:*.log\t${path}|${noIndex}`
        : `${path}|${noIndex}`
    );
  }

  catFile(_path: string, objectName: string, mode: string): string {
    if (mode === 'type') {
      return 'commit';
    }
    if (mode === 'size') {
      return '123';
    }
    if (mode === 'exists') {
      return '';
    }
    return `${mode}:${objectName}`;
  }

  listTree(
    _path: string,
    treeish: string,
    options: RepositoryListTreeOptions
  ): string[] {
    return [
      treeish,
      `${options.recursive}`,
      `${options.directoriesOnly}`,
      `${options.includeTrees}`,
      `${options.nameOnly}`,
      `${options.objectOnly}`,
      `${options.longFormat}`,
      `${options.fullName}`,
      `${options.fullTree}`,
      options.paths.join(',')
    ];
  }

  showRef(
    _path: string,
    options: RepositoryShowRefOptions
  ): string[] {
    const requested: string = options.patterns[0] ?? '';
    if (requested.startsWith('refs/remotes/')) {
      const prefix: string = requested.endsWith('/')
        ? requested
        : requested.slice(0, requested.lastIndexOf('/') + 1);
      if (requested.endsWith('/HEAD')) {
        return [
          `${this.remoteObjectId} ` +
          requested
        ];
      }
      if (requested.endsWith('/main')) {
        return [
          `${this.remoteObjectId} ` +
          requested
        ];
      }
      return [
        `${this.remoteObjectId} ` +
        `${prefix}HEAD`,
        `${this.remoteObjectId} ` +
        `${prefix}main`
      ];
    }
    return [
      `${options.heads}`,
      `${options.tags}`,
      `${options.includeHead}`,
      `${options.dereference}`,
      `${options.verify}`,
      `${options.quiet}`,
      `${options.hashOnly}`,
      `${options.abbreviation}`,
      options.patterns.join(',')
    ];
  }

  excludeExistingRefs(
    _path: string,
    input: string,
    pattern: string
  ): string[] {
    return [`${pattern}|${input}`];
  }

  revList(
    _path: string,
    options: RepositoryRevListOptions
  ): string[] {
    const pathSuffix: string = options.paths.length === 0
      ? ''
      : `|${options.paths.join(',')}`;
    return [
      `${options.all}|${options.branches}|${options.tags}|${options.remotes}`,
      `${options.parents}|${options.count}|${options.reverse}|${options.firstParent}`,
      `${options.noMerges}|${options.merges}|${options.abbreviate}|${options.abbreviation}|${options.maxCount}`,
      `${options.revisions.join(',')}${pathSuffix}`
    ];
  }

  mergeBase(
    _path: string,
    options: RepositoryMergeBaseOptions
  ): string[] {
    if (options.revisions.includes('unrelated')) {
      return [];
    }
    return [
      `${options.all}|${options.octopus}|${options.independent}`,
      options.revisions.join(',')
    ];
  }

  isAncestor(
    _path: string,
    ancestor: string,
    descendant: string
  ): boolean {
    if (ancestor === this.currentHeadObjectId &&
        descendant === this.remoteObjectId) {
      return this.fastForwardAllowed;
    }
    return ancestor === 'main' && descendant === 'topic';
  }

  findForkPoint(
    _path: string,
    reference: string,
    derived: string
  ): string {
    return reference === 'main' && derived === 'topic'
      ? 'fork-point'
      : '';
  }

  forEachRef(
    _path: string,
    options: RepositoryForEachRefOptions
  ): string[] {
    return [
      `${options.count}|${options.format}|${options.ignoreCase}|${options.includeRootRefs}`,
      options.sortKeys.join(','),
      options.patterns.join(','),
      options.excludes.join(','),
      `${options.pointsAt}|${options.merged}|${options.noMerged}|${options.contains}|${options.noContains}`
    ];
  }

  symbolicRef(
    _path: string,
    name: string,
    shortName: boolean,
    recurse: boolean
  ): string {
    if (name.startsWith('refs/remotes/') &&
        name.endsWith('/HEAD')) {
      return `${name.slice(0, name.length - 'HEAD'.length)}main`;
    }
    return `${name}|${shortName}|${recurse}`;
  }

  updateSymbolicRef(
    path: string,
    name: string,
    target: string,
    _deleteReference: boolean,
    _message: string
  ): RepositoryOperation {
    this.lastSymbolicRefPath = path;
    this.lastSymbolicRefName = name;
    this.lastSymbolicRefTarget = target;
    return this.operation(path, 1);
  }

  updateRef(
    path: string,
    name: string,
    newValue: string,
    _oldValue: string,
    _deleteReference: boolean,
    _noDeref: boolean,
    _message: string,
    _createReflog: boolean
  ): RepositoryOperation {
    this.lastUpdateRefPath = path;
    this.lastUpdateRefName = name;
    this.lastUpdateRefValue = newValue;
    return this.operation(path, 1);
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
    const operation: RepositoryOperation = this.operation(path, 2);
    const visibleInput: string =
      input.replace(/\n/g, ',')
        .split(String.fromCharCode(0))
        .join('<NUL>');
    operation.output = [
      `${noDeref}|${createReflog}|${nullTerminated}|${batchUpdates}|` +
      `${message}|` +
      visibleInput
    ];
    return operation;
  }

  createTag(
    path: string,
    name: string,
    target: string,
    force: boolean,
    annotated: boolean,
    message: string
  ): RepositoryOperation {
    this.lastTagName = name;
    this.lastTagTarget = target;
    this.lastTagForce = force;
    this.lastTagAnnotated = annotated;
    this.lastTagMessage = message;
    this.tagCreateCount++;
    return this.operation(path, 1);
  }

  deleteTags(path: string, names: string[]): RepositoryOperation {
    return this.operation(path, names.length);
  }

  readConfig(
    _path: string,
    scope: string,
    includes: boolean,
    explicitFile: string = ''
  ): RepositoryConfigEntry[] {
    this.lastConfigReadScope = scope;
    this.lastConfigReadIncludes = includes;
    this.lastConfigReadFile = explicitFile;
    const entries: RepositoryConfigEntry[] = [
      { key: 'user.name', value: 'Harmony Test' },
      { key: 'user.email', value: 'harmony@example.invalid' },
      { key: 'remote.origin.url', value: 'https://example.invalid/repository.git' }
    ];
    if (this.typedConfig) {
      entries.push(
        { key: 'types.bool', value: 'yes' },
        { key: 'types.int', value: '2k' },
        { key: 'types.bool-or-int', value: 'off' },
        { key: 'types.bool-or-str', value: 'maybe' },
        { key: 'types.path', value: '~/repo' },
        { key: 'types.expiry', value: 'never' },
        { key: 'types.bare', value: '' }
      );
    }
    if (this.multivarConfig) {
      entries.push(
        { key: 'alias.echo', value: 'first' },
        { key: 'alias.echo', value: 'second' }
      );
      if (includes) {
        entries.push({ key: 'included.value', value: 'enabled' });
      }
    }
    if (this.branchUpstreamConfigured) {
      entries.push(
        { key: 'branch.main.remote', value: 'origin' },
        { key: 'branch.main.merge', value: 'refs/heads/main' }
      );
    }
    if (scope === 'all') {
      entries.push(...this.commandConfigEntries);
    }
    return entries;
  }

  setConfig(
    path: string,
    key: string,
    value: string,
    scope: string,
    append: boolean,
    explicitFile: string = ''
  ): RepositoryOperation {
    this.lastConfigKey = key;
    this.lastConfigValue = value;
    this.lastConfigScope = scope;
    this.lastConfigAppend = append;
    this.lastConfigWriteFile = explicitFile;
    this.configWrites.push({ key, value });
    return this.operation(path, 1);
  }

  unsetConfig(
    path: string,
    _key: string,
    scope: string,
    all: boolean,
    explicitFile: string = ''
  ): RepositoryOperation {
    this.lastUnsetConfigScope = scope;
    this.lastUnsetConfigAll = all;
    this.lastUnsetConfigFile = explicitFile;
    return this.operation(path, 1);
  }

  setCommandConfig(entries: RepositoryConfigEntry[]): void {
    this.commandConfigEntries = entries.map(
      (entry: RepositoryConfigEntry): RepositoryConfigEntry => ({
        key: entry.key,
        value: entry.value
      })
    );
    if (entries.length === 0) {
      this.commandConfigClearCount++;
    } else {
      this.commandConfigApplicationCount++;
    }
  }

  addRemote(
    path: string,
    name: string,
    url: string
  ): RepositoryOperation {
    this.lastRemotePath = path;
    this.lastRemoteName = name;
    this.lastRemoteUrl = url;
    return this.operation(path, 1);
  }

  removeRemote(path: string, _name: string): RepositoryOperation {
    return this.operation(path, 1);
  }

  renameRemote(
    path: string,
    _oldName: string,
    _newName: string
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  getRemoteUrl(_path: string, _name: string, push: boolean): string {
    return push
      ? 'ssh://example.invalid/repository.git'
      : 'https://example.invalid/repository.git';
  }

  setRemoteUrl(
    path: string,
    _name: string,
    _url: string,
    _push: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  listRemoteReferences(
    _url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    _patterns: string[]
  ): RepositoryRemoteAdvertisement {
    const allReferences: RepositoryRemoteReference[] = [];
    allReferences.push({
      objectId: '1234567890abcdef1234567890abcdef12345678',
      name: 'HEAD'
    });
    allReferences.push({
      objectId: '1234567890abcdef1234567890abcdef12345678',
      name: 'refs/heads/main'
    });
    allReferences.push({
      objectId: 'abcdef1234567890abcdef1234567890abcdef12',
      name: 'refs/tags/v1.0'
    });
    allReferences.push({
      objectId: '1234567890abcdef1234567890abcdef12345678',
      name: 'refs/tags/v1.0^{}'
    });
    const references: RepositoryRemoteReference[] =
      allReferences.filter(
        (reference: RepositoryRemoteReference): boolean => {
      const isHead: boolean =
        reference.name.startsWith('refs/heads/');
      const isTag: boolean =
        reference.name.startsWith('refs/tags/');
      if ((heads || tags) &&
          !((heads && isHead) || (tags && isTag))) {
        return false;
      }
      return !refsOnly ||
        (reference.name !== 'HEAD' &&
         !reference.name.endsWith('^{}'));
        }
      );
    return {
      success: true,
      responseCode: 200,
      headTarget: 'refs/heads/main',
      references,
      capabilities: ['multi_ack_detailed', 'side-band-64k', 'ofs-delta'],
      error: ''
    };
  }

  buildRemoteUploadPackUrl(url: string): string {
    return `${url}/git-upload-pack`;
  }

  listRemotePushReferences(
    url: string,
    heads: boolean,
    tags: boolean,
    refsOnly: boolean,
    patterns: string[]
  ): RepositoryRemoteAdvertisement {
    const advertisement: RepositoryRemoteAdvertisement =
      this.listRemoteReferences(
        url,
        heads,
        tags,
        refsOnly,
        patterns
      );
    advertisement.capabilities.push('report-status');
    return advertisement;
  }

  buildRemoteReceivePackUrl(url: string): string {
    return `${url}/git-receive-pack`;
  }

  buildUploadPackRequest(
    _wants: string[],
    _haves: string[],
    _capabilities: string[]
  ): ArrayBuffer {
    return new ArrayBuffer(0);
  }

  parseUploadPackResponse(
    _payload: ArrayBuffer
  ): RepositoryRemotePackResponse {
    return {
      success: true,
      acknowledged: false,
      objectCount: 0,
      packData: new ArrayBuffer(0),
      progress: '',
      error: ''
    };
  }

  buildReceivePackRequest(
    _path: string,
    _updates: RepositoryRemotePushUpdate[],
    _haves: string[],
    _capabilities: string[]
  ): ArrayBuffer {
    return new ArrayBuffer(0);
  }

  parseReceivePackResponse(
    _payload: ArrayBuffer,
    _expectedReferences: string[]
  ): RepositoryRemotePushResult {
    return {
      success: true,
      unpacked: true,
      output: ['ok refs/heads/main'],
      error: ''
    };
  }

  installRemotePack(
    path: string,
    _remoteName: string,
    _packData: ArrayBuffer,
    _references: RepositoryRemoteReference[],
    _headTarget: string
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  async fetch(
    path: string,
    remoteName: string
  ): Promise<RepositoryOperation> {
    this.lastFetchPath = path;
    this.lastFetchRemote = remoteName;
    const operation: RepositoryOperation = this.operation(path, 2);
    operation.output = [
      `From ${remoteName}`,
      ` * [fetched] main -> ${remoteName}/main`,
      'Installed 3 objects from pack-test.'
    ];
    return operation;
  }

  async push(
    path: string,
    remoteName: string,
    updates: RepositoryRemotePushUpdate[],
    force: boolean
  ): Promise<RepositoryOperation> {
    this.lastPushPath = path;
    this.lastPushRemote = remoteName;
    this.lastPushUpdates = updates.slice();
    this.lastPushForce = force;
    const operation: RepositoryOperation = this.operation(path, 1);
    operation.output = [
      'unpack ok',
      `ok ${updates[0]?.name ?? 'refs/heads/main'}`
    ];
    return operation;
  }

  reflog(
    _path: string,
    ref: string,
    maxCount: number,
    skip: number = 0,
    since: string = '',
    until: string = ''
  ): RepositoryReflogEntry[] {
    this.lastReflogRef = ref;
    this.lastReflogMaxCount = maxCount;
    this.lastReflogSkip = skip;
    this.lastReflogSince = since;
    this.lastReflogUntil = until;
    return [{
      oldId: '0000000000000000000000000000000000000000',
      newId: '1234567890abcdef1234567890abcdef12345678',
      actor: 'Harmony Test <harmony@example.invalid>',
      timestamp: '1700000000 +0000',
      message: 'commit: native test',
      index: 0,
      selector: `${ref.length === 0 ? 'HEAD' : ref}@{0}`,
      subject: 'native test',
      author: 'Harmony Test <harmony@example.invalid>',
      committer: 'Harmony Committer <committer@example.invalid>',
      authorTimestamp: '1699999900 +0000',
      commitTimestamp: '1700000000 +0000'
    }];
  }

  listReflogs(_path: string): string[] {
    return ['HEAD', 'refs/heads/main'];
  }

  reflogExists(_path: string, ref: string): boolean {
    return ref === 'HEAD' || ref === 'refs/heads/main';
  }

  writeReflog(
    path: string,
    ref: string,
    oldObjectId: string,
    newObjectId: string,
    message: string
  ): RepositoryOperation {
    this.lastReflogWriteRef = ref;
    this.lastReflogWriteOldObjectId = oldObjectId;
    this.lastReflogWriteNewObjectId = newObjectId;
    this.lastReflogWriteMessage = message;
    return this.operation(path, 1);
  }

  deleteReflogEntries(
    path: string,
    selectors: string[],
    rewrite: boolean,
    updateRef: boolean,
    dryRun: boolean,
    verbose: boolean
  ): RepositoryOperation {
    this.lastReflogDeleteSelectors = selectors.slice();
    this.lastReflogDeleteRewrite = rewrite;
    this.lastReflogDeleteUpdateRef = updateRef;
    this.lastReflogDeleteDryRun = dryRun;
    this.lastReflogDeleteVerbose = verbose;
    const operation: RepositoryOperation = this.operation(path, 1);
    operation.output = verbose
      ? ['keep commit (initial): one', 'would prune commit: two']
      : [];
    return operation;
  }

  dropReflogs(
    path: string,
    refs: string[],
    all: boolean,
    singleWorktree: boolean
  ): RepositoryOperation {
    this.lastReflogDropRefs = refs.slice();
    this.lastReflogDropAll = all;
    this.lastReflogDropSingleWorktree = singleWorktree;
    return this.operation(path, refs.length > 0 || all ? 1 : 0);
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
    this.lastReflogExpireRefs = refs.slice();
    this.lastReflogExpire = expire;
    this.lastReflogExpireUnreachable = expireUnreachable;
    this.lastReflogExpireRewrite = rewrite;
    this.lastReflogExpireUpdateRef = updateRef;
    this.lastReflogExpireStaleFix = staleFix;
    this.lastReflogExpireDryRun = dryRun;
    this.lastReflogExpireVerbose = verbose;
    this.lastReflogExpireAll = all;
    this.lastReflogExpireSingleWorktree = singleWorktree;
    const operation: RepositoryOperation = this.operation(path, 1);
    operation.output = verbose
      ? ['keep commit (initial): one', 'would prune commit: two']
      : [];
    return operation;
  }

  private snapshot(repositoryPath: string): RepositorySnapshot {
    return {
      valid: true,
      repositoryPath,
      gitDirectory: `${repositoryPath}/.git`,
      branch: 'main',
      head: this.currentHeadObjectId,
      detached: false,
      indexVersion: 2,
      files: [
        {
          path: 'README.md',
          indexState: ' ',
          workTreeState: 'M',
          tracked: true,
          staged: false
        },
        {
          path: 'staged.txt',
          indexState: 'A',
          workTreeState: ' ',
          tracked: true,
          staged: true
        },
        {
          path: 'notes.txt',
          indexState: '?',
          workTreeState: '?',
          tracked: false,
          staged: false
        }
      ],
      branches: ['main', 'release'],
      remotes: [{
        name: 'origin',
        fetchUrl: 'https://example.invalid/repository.git',
        pushUrl: 'ssh://example.invalid/repository.git'
      }],
      error: ''
    };
  }

  private invalid(path: string): RepositorySnapshot {
    return {
      valid: false,
      repositoryPath: '',
      gitDirectory: '',
      branch: '',
      head: '',
      detached: false,
      indexVersion: 0,
      files: [],
      branches: [],
      remotes: [],
      error: `Not a Git repository: ${path}`
    };
  }

  private operation(path: string, changedCount: number): RepositoryOperation {
    return {
      success: true,
      changedCount,
      snapshot: this.snapshot(path),
      output: [],
      error: ''
    };
  }
}

export default function gitShellTest(): void {
  describe('GitShell', (): void => {
    it('shows the initial working tree in porcelain form', 0, (): void => {
      const shell: GitShell = new GitShell();
      const status: string = shell.execute('git status --short');
      expect(status).assertEqual(' M src/main/ets/pages/Index.ets\n M README.md\n?? docs/porting-notes.md');
    });

    it('stages files and creates a commit with a quoted message', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('git add README.md')).assertEqual('Staged 1 file.');
      const committed: string = shell.execute('git commit -m "Document Harmony port"');
      expect(committed.includes('[main ')).assertTrue();
      expect(committed.includes('Document Harmony port')).assertTrue();
      const remaining: string = shell.execute('git status --short');
      expect(remaining.includes('README.md')).assertFalse();
      expect(remaining.includes('src/main/ets/pages/Index.ets')).assertTrue();
    });

    it('stages every file with the standard all flag', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('git add -A')).assertEqual('Staged 3 files.');
      const status: string = shell.execute('git status --short');
      expect(status.includes('??')).assertFalse();
      expect(status.split('\n').length).assertEqual(3);
    });

    it('creates and switches branches using git switch', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('git switch -c feature/terminal')).assertEqual("Switched to a new branch 'feature/terminal'");
      expect(shell.prompt().includes('(feature/terminal)')).assertTrue();
      expect(shell.execute('git branch').includes('* feature/terminal')).assertTrue();
    });

    it('supports environment expansion and single-command assignments', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('export PORT=8080')).assertEqual('');
      expect(shell.execute('echo "$PORT:$PWD"')).assertEqual(
        '8080:/home/harmony/workspace/demo-app\n'
      );
      expect(shell.execute("echo '$PORT'")).assertEqual('$PORT\n');
      expect(shell.execute('TEMP=local echo "$TEMP"')).assertEqual('local\n');
      expect(shell.execute('echo "$TEMP"')).assertEqual('\n');
      expect(shell.execute('unset PORT')).assertEqual('');
      expect(shell.execute('echo "$PORT"')).assertEqual('\n');
    });

    it('supports env commands and requested printenv values', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('printenv HOME')).assertEqual('/home/harmony');
      expect(shell.execute('env SESSION=temporary printenv SESSION')).assertEqual(
        'temporary'
      );
      expect(shell.execute('printenv SESSION')).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(1);
      expect(
        shell.execute('missing-command').includes('command not found')
      ).assertTrue();
      expect(shell.execute('echo "$?"')).assertEqual('127\n');
    });

    it('supports stdout and stdin redirection in the compatibility session', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute("printf 'first\\n' > output.txt")).assertEqual('');
      expect(shell.execute("printf 'second\\n' >> output.txt")).assertEqual('');
      expect(shell.execute('cat output.txt')).assertEqual('first\nsecond\n');
      expect(shell.execute('cat < output.txt')).assertEqual(
        'first\nsecond\n'
      );
      expect(
        shell.execute('cat < missing.txt').includes(
          'No such file or directory'
        )
      ).assertTrue();
      expect(shell.lastExitCode()).assertEqual(1);
      expect(shell.execute('echo "$?"')).assertEqual('1\n');
    });

    it('keeps destructive file restoration behind the native service', 0, (): void => {
      const shell: GitShell = new GitShell();
      expect(shell.execute('git reset --hard').includes('native file service')).assertTrue();
    });

    it('formats real repository commands through the native service boundary', 0, (): void => {
      const shell: GitShell = new GitShell();
      const ready: string = shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(ready).assertEqual(
        'Native repository ready: /sandbox/workspace/demo-app'
      );
      expect(shell.execute('git status --short')).assertEqual(
        ' M README.md\nA  staged.txt\n?? notes.txt'
      );
      expect(shell.execute('git branch')).assertEqual(
        '* main\n  release'
      );
      expect(shell.execute('git remote -v')).assertEqual(
        'origin\thttps://example.invalid/repository.git (fetch)\n' +
        'origin\tssh://example.invalid/repository.git (push)'
      );
      expect(shell.execute('git rev-parse --show-toplevel')).assertEqual(
        '/sandbox/workspace/demo-app'
      );
      expect(shell.execute('git rev-parse HEAD')).assertEqual(
        '1234567890abcdef1234567890abcdef12345678'
      );
    });

    it('routes branch move and copy commands through the native service', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git branch -m main renamed')).assertEqual('');
      expect(shell.execute('git branch -M main renamed')).assertEqual('');
      expect(shell.execute('git branch -c main copied')).assertEqual('');
      expect(shell.execute('git branch -C main copied')).assertEqual('');
    });

    it('routes remote management commands through the native service', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git remote add upstream https://example.invalid/upstream.git'
      )).assertEqual('');
      expect(shell.execute('git remote remove origin')).assertEqual('');
      expect(shell.execute('git remote rm origin')).assertEqual('');
      expect(shell.execute('git remote rename origin upstream')).assertEqual('');
      expect(shell.execute('git remote get-url origin')).assertEqual(
        'https://example.invalid/repository.git'
      );
      expect(shell.execute('git remote get-url --push origin')).assertEqual(
        'ssh://example.invalid/repository.git'
      );
      expect(shell.execute(
        'git remote set-url origin https://example.invalid/new.git'
      )).assertEqual('');
      expect(shell.execute(
        'git remote set-url --push origin ssh://example.invalid/new.git'
      )).assertEqual('');
    });

    it('lists remote references through the NetworkKit boundary', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git ls-remote --symref origin')).assertEqual(
        'ref: refs/heads/main\tHEAD\n' +
        '1234567890abcdef1234567890abcdef12345678\tHEAD\n' +
        '1234567890abcdef1234567890abcdef12345678\trefs/heads/main\n' +
        'abcdef1234567890abcdef1234567890abcdef12\trefs/tags/v1.0\n' +
        '1234567890abcdef1234567890abcdef12345678\trefs/tags/v1.0^{}'
      );
      expect(shell.execute('git ls-remote --heads origin')).assertEqual(
        '1234567890abcdef1234567890abcdef12345678\trefs/heads/main'
      );
      expect(shell.execute('git ls-remote --refs --tags origin')).assertEqual(
        'abcdef1234567890abcdef1234567890abcdef12\trefs/tags/v1.0'
      );
      expect(shell.execute('git ls-remote --get-url origin')).assertEqual(
        'https://example.invalid/repository.git'
      );
    });

    it('fetches remote packs through the asynchronous service boundary', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(
          service,
          '/sandbox'
        );
        const applicationsBefore: number =
          service.commandConfigApplicationCount;
        const clearsBefore: number = service.commandConfigClearCount;
        expect(await shell.executeAsync(
          'git -c http.extraHeader=Harmony fetch upstream'
        )).assertEqual(
          'From upstream\n' +
          ' * [fetched] main -> upstream/main\n' +
          'Installed 3 objects from pack-test.'
        );
        expect(service.commandConfigApplicationCount)
          .assertEqual(applicationsBefore + 1);
        expect(service.commandConfigClearCount)
          .assertEqual(clearsBefore + 1);
        expect(service.commandConfigEntries.length).assertEqual(0);
        expect(shell.lastExitCode()).assertEqual(0);
        expect(await shell.executeAsync(
          'git fetch upstream unexpected'
        )).assertEqual('usage: git fetch [<remote>]');
        expect(shell.lastExitCode()).assertEqual(129);
      });

    it('pushes a branch through the asynchronous service boundary', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        service.currentHeadObjectId =
          'fedcba9876543210fedcba9876543210fedcba98';
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(service, '/sandbox');
        const output: string =
          await shell.executeAsync('git push origin main');
        expect(output.includes(
          'To ssh://example.invalid/repository.git'
        )).assertTrue();
        expect(output.includes(
          '[new branch] main -> main'
        )).assertTrue();
        expect(service.lastPushPath).assertEqual(
          '/sandbox/workspace/demo-app'
        );
        expect(service.lastPushRemote).assertEqual('origin');
        expect(service.lastPushUpdates.length).assertEqual(1);
        expect(service.lastPushUpdates[0].newObjectId).assertEqual(
          service.currentHeadObjectId
        );
        expect(service.lastPushUpdates[0].name).assertEqual(
          'refs/heads/main'
        );
        expect(service.lastPushForce).assertEqual(false);
        expect(shell.lastExitCode()).assertEqual(0);
      });

    it('sets push upstream and deletes a remote branch with force', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(service, '/sandbox');
        const upstream: string =
          await shell.executeAsync('git push -u origin main:main');
        expect(upstream.includes(
          "branch 'main' set up to track 'origin/main'."
        )).assertTrue();
        expect(service.lastConfigKey).assertEqual(
          'branch.main.merge'
        );
        expect(service.lastConfigValue).assertEqual(
          'refs/heads/main'
        );
        expect(await shell.executeAsync(
          'git push --force origin :topic'
        )).assertEqual(
          'To ssh://example.invalid/repository.git\n' +
          ' - [deleted] topic'
        );
        expect(service.lastPushForce).assertEqual(true);
        expect(service.lastPushUpdates[0].newObjectId).assertEqual(
          '0000000000000000000000000000000000000000'
        );
        expect(service.lastPushUpdates[0].name).assertEqual(
          'refs/heads/topic'
        );
        expect(shell.lastExitCode()).assertEqual(0);
      });

    it('rejects malformed push arguments and missing source refs', 0,
      async (): Promise<void> => {
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(
          new MockGitRepositoryService(),
          '/sandbox'
        );
        expect(await shell.executeAsync(
          'git push origin main:main extra'
        )).assertEqual(
          'usage: git push [-u] [-f] [<repository> [<refspec>]]'
        );
        expect(shell.lastExitCode()).assertEqual(129);
        expect(await shell.executeAsync(
          'git push origin missing'
        )).assertEqual(
          'error: src refspec missing does not match any.'
        );
        expect(shell.lastExitCode()).assertEqual(128);
      });

    it('clones an HTTPS repository and keeps the shell working directory', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(service, '/sandbox');
        expect(shell.execute('cd ../..')).assertEqual('');
        expect(await shell.executeAsync(
          'git clone https://example.invalid/repository.git'
        )).assertEqual(
          "Cloning into 'repository'...\n" +
          'From origin\n' +
          ' * [fetched] main -> origin/main\n' +
          'Installed 3 objects from pack-test.'
        );
        expect(service.lastInitializePath).assertEqual(
          '/sandbox/repository'
        );
        expect(service.lastRemotePath).assertEqual('/sandbox/repository');
        expect(service.lastRemoteName).assertEqual('origin');
        expect(service.lastRemoteUrl).assertEqual(
          'https://example.invalid/repository.git'
        );
        expect(service.lastFetchPath).assertEqual('/sandbox/repository');
        expect(service.lastFetchRemote).assertEqual('origin');
        expect(service.lastCheckoutPath).assertEqual('/sandbox/repository');
        expect(service.lastCheckoutName).assertEqual('main');
        expect(service.lastCheckoutStartPoint).assertEqual(
          'refs/remotes/origin/main'
        );
        expect(service.configWrites.length).assertEqual(2);
        expect(service.configWrites[0].key).assertEqual(
          'branch.main.remote'
        );
        expect(service.configWrites[0].value).assertEqual('origin');
        expect(service.configWrites[1].key).assertEqual(
          'branch.main.merge'
        );
        expect(service.configWrites[1].value).assertEqual(
          'refs/heads/main'
        );
        expect(shell.execute('pwd')).assertEqual('/sandbox');
        expect(shell.lastExitCode()).assertEqual(0);
      });

    it('supports clone without checkout and rejects non-empty targets', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(service, '/sandbox');
        expect(shell.execute('cd ../..')).assertEqual('');
        expect(await shell.executeAsync(
          'git clone -n -o mirror ' +
          'https://example.invalid/repository.git copy'
        )).assertEqual(
          "Cloning into 'copy'...\n" +
          'From mirror\n' +
          ' * [fetched] main -> mirror/main\n' +
          'Installed 3 objects from pack-test.'
        );
        expect(service.lastCheckoutPath).assertEqual('');
        expect(service.lastUpdateRefPath).assertEqual('/sandbox/copy');
        expect(service.lastUpdateRefName).assertEqual('refs/heads/main');
        expect(service.lastUpdateRefValue).assertEqual(
          service.remoteObjectId
        );
        expect(service.lastSymbolicRefPath).assertEqual('/sandbox/copy');
        expect(service.lastSymbolicRefName).assertEqual('HEAD');
        expect(service.lastSymbolicRefTarget).assertEqual(
          'refs/heads/main'
        );
        expect(service.configWrites[0].value).assertEqual('mirror');
        expect(await shell.executeAsync(
          'git clone https://example.invalid/repository.git existing'
        )).assertEqual(
          "fatal: destination path 'existing' already exists and " +
          'is not an empty directory.'
        );
        expect(shell.lastExitCode()).assertEqual(128);
      });

    it('pulls the configured upstream with a fast-forward update', 0,
      async (): Promise<void> => {
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        service.branchUpstreamConfigured = true;
        const shell: GitShell = new GitShell();
        shell.attachRepositoryService(service, '/sandbox');
        expect(await shell.executeAsync('git pull')).assertEqual(
          'From origin\n' +
          ' * [fetched] main -> origin/main\n' +
          'Installed 3 objects from pack-test.\n' +
          'Updating 1234567..abcdef1\n' +
          'Fast-forward'
        );
        expect(service.lastFetchPath).assertEqual(
          '/sandbox/workspace/demo-app'
        );
        expect(service.lastFetchRemote).assertEqual('origin');
        expect(service.lastCheckoutPath).assertEqual(
          '/sandbox/workspace/demo-app'
        );
        expect(service.lastCheckoutName).assertEqual('main');
        expect(service.lastCheckoutStartPoint).assertEqual(
          'refs/remotes/origin/main'
        );
        expect(shell.lastExitCode()).assertEqual(0);
      });

    it('reports up-to-date and divergent pull states without overwriting', 0,
      async (): Promise<void> => {
        const current: MockGitRepositoryService =
          new MockGitRepositoryService();
        current.branchUpstreamConfigured = true;
        current.currentHeadObjectId = current.remoteObjectId;
        const currentShell: GitShell = new GitShell();
        currentShell.attachRepositoryService(current, '/sandbox');
        expect(await currentShell.executeAsync('git pull')).assertEqual(
          'From origin\n' +
          ' * [fetched] main -> origin/main\n' +
          'Installed 3 objects from pack-test.\n' +
          'Already up to date.'
        );
        expect(current.lastCheckoutPath).assertEqual('');

        const divergent: MockGitRepositoryService =
          new MockGitRepositoryService();
        divergent.branchUpstreamConfigured = true;
        divergent.fastForwardAllowed = false;
        const divergentShell: GitShell = new GitShell();
        divergentShell.attachRepositoryService(divergent, '/sandbox');
        expect(await divergentShell.executeAsync('git pull')).assertEqual(
          'fatal: Not possible to fast-forward, aborting.'
        );
        expect(divergent.lastCheckoutPath).assertEqual('');
        expect(divergentShell.lastExitCode()).assertEqual(128);
        expect(await divergentShell.executeAsync(
          'git pull origin main extra'
        )).assertEqual('usage: git pull [<remote>] [<branch>]');
        expect(divergentShell.lastExitCode()).assertEqual(129);
      });

    it('uses real directory navigation and routes writes through native service', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('ls')).assertEqual('.git/  README.md  src/');
      expect(shell.execute('cd src')).assertEqual('');
      expect(shell.execute('pwd')).assertEqual(
        '/sandbox/workspace/demo-app/src'
      );
      expect(shell.execute('git init')).assertEqual(
        'Reinitialized empty Git repository in ' +
        '/sandbox/workspace/demo-app/.git/'
      );
      expect(shell.execute('git add README.md')).assertEqual('Staged 1 file.');
      expect(shell.execute('git open /external/repository')).assertEqual(
        'Opened Git repository: /external/repository'
      );
      expect(shell.execute('git open /missing').startsWith('fatal:')).assertTrue();
    });

    it('persists redirection through the native repository service boundary', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute("printf 'native\\n' > notes.txt")).assertEqual('');
      expect(shell.execute('cat notes.txt')).assertEqual('native\n');
    });

    it('restores the working tree and supports checkout path syntax', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git restore README.md')).assertEqual(
        'Restored 1 file.'
      );
      expect(shell.execute('git restore --staged staged.txt')).assertEqual(
        'Unstaged 1 file.'
      );
      expect(shell.execute('git checkout -- README.md')).assertEqual(
        'Restored 1 file.'
      );
      expect(
        shell.execute(
          'git restore --source=HEAD~1 --staged --worktree README.md'
        )
      ).assertEqual(
        'Restored 1 file to the index and working tree.'
      );
      expect(
        shell.execute('git restore --source HEAD~1 -- README.md')
      ).assertEqual('Restored 1 file.');
      expect(shell.execute('git checkout -B release HEAD~1')).assertEqual(
        "Switched to and reset branch 'release'"
      );
      expect(shell.execute('git switch -C release HEAD~1')).assertEqual(
        "Switched to and reset branch 'release'"
      );
    });

    it('never falls back to simulated Git after native attachment', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('cd ../../empty')).assertEqual('');
      expect(shell.execute('git branch')).assertEqual(
        'fatal: not a git repository (or any parent directories): .git'
      );
      expect(shell.execute('git add README.md')).assertEqual(
        'fatal: not a git repository (or any parent directories): .git'
      );
      expect(shell.execute('git init')).assertEqual(
        'Initialized empty Git repository in /sandbox/empty/.git/'
      );
    });

    it('reads and updates local Git config through the native service', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(service, '/sandbox');
      expect(shell.execute('git config --list')).assertEqual(
        'user.name\tHarmony Test\n' +
        'user.email\tharmony@example.invalid\n' +
        'remote.origin.url\thttps://example.invalid/repository.git'
      );
      expect(service.lastConfigReadScope).assertEqual('all');
      expect(service.lastConfigReadIncludes).assertTrue();
      expect(shell.execute('git config --get user.name')).assertEqual(
        'Harmony Test'
      );
      expect(shell.execute('git config user.name "Updated Test"')).assertEqual('');
      expect(service.lastConfigScope).assertEqual('local');
      expect(service.lastConfigAppend).assertFalse();
      expect(shell.execute('git config --unset core.editor')).assertEqual('');
      expect(service.lastUnsetConfigScope).assertEqual('local');
      expect(service.lastUnsetConfigAll).assertFalse();
    });

    it('routes config scopes, includes and multivars through the native service', 0,
      (): void => {
        const shell: GitShell = new GitShell();
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        service.multivarConfig = true;
        shell.attachRepositoryService(service, '/sandbox');

        expect(
          shell.execute(
            'git config --global --no-includes --get user.name'
          )
        ).assertEqual('Harmony Test');
        expect(service.lastConfigReadScope).assertEqual('global');
        expect(service.lastConfigReadIncludes).assertFalse();
        expect(shell.execute('git config --get alias.echo')).assertEqual(
          'second'
        );
        expect(shell.execute('git config --get-all alias.echo')).assertEqual(
          'first\nsecond'
        );
        expect(
          shell.execute('git config --local --includes --get included.value')
        ).assertEqual('enabled');
        expect(service.lastConfigReadScope).assertEqual('local');
        expect(service.lastConfigReadIncludes).assertTrue();

        expect(
          shell.execute('git config --global --add alias.echo third')
        ).assertEqual('');
        expect(service.lastConfigScope).assertEqual('global');
        expect(service.lastConfigAppend).assertTrue();
        expect(
          shell.execute('git config --system --unset-all alias.echo')
        ).assertEqual('');
        expect(service.lastUnsetConfigScope).assertEqual('system');
        expect(service.lastUnsetConfigAll).assertTrue();
    });

    it('applies and clears command-scoped Git config overrides', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(service, '/sandbox');
      const applicationsBefore: number =
        service.commandConfigApplicationCount;
      const clearsBefore: number = service.commandConfigClearCount;

      expect(
        shell.execute(
          'git -c user.name="Command User" -c feature.enabled ' +
          'config --get user.name'
        )
      ).assertEqual('Command User');
      expect(service.commandConfigApplicationCount)
        .assertEqual(applicationsBefore + 1);
      expect(service.commandConfigClearCount)
        .assertEqual(clearsBefore + 1);
      expect(service.commandConfigEntries.length).assertEqual(0);
      expect(shell.execute('git config --get user.name')).assertEqual(
        'Harmony Test'
      );
      expect(
        shell.execute(
          'git -c feature.enabled config --bool --get feature.enabled'
        )
      ).assertEqual('true');
    });

    it('routes explicit config files through native reads and writes', 0,
      (): void => {
        const shell: GitShell = new GitShell();
        const service: MockGitRepositoryService =
          new MockGitRepositoryService();
        shell.attachRepositoryService(service, '/sandbox');

        expect(
          shell.execute(
            'git config --file ../custom.gitconfig --get user.name'
          )
        ).assertEqual('Harmony Test');
        expect(service.lastConfigReadScope).assertEqual('file');
        expect(service.lastConfigReadIncludes).assertFalse();
        expect(service.lastConfigReadFile).assertEqual(
          '/sandbox/workspace/custom.gitconfig'
        );
        expect(
          shell.execute(
            'git config -f ../custom.gitconfig user.name "File User"'
          )
        ).assertEqual('');
        expect(service.lastConfigScope).assertEqual('file');
        expect(service.lastConfigWriteFile).assertEqual(
          '/sandbox/workspace/custom.gitconfig'
        );
        expect(
          shell.execute(
            'git config --file=../custom.gitconfig --unset user.name'
          )
        ).assertEqual('');
        expect(service.lastUnsetConfigScope).assertEqual('file');
        expect(service.lastUnsetConfigFile).assertEqual(
          '/sandbox/workspace/custom.gitconfig'
        );
    });

    it('canonicalizes supported Git config value types', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      service.typedConfig = true;
      shell.attachRepositoryService(service, '/sandbox');

      expect(shell.execute('git config --bool --get types.bool'))
        .assertEqual('true');
      expect(shell.execute('git config --bool --get types.bare'))
        .assertEqual('true');
      expect(shell.execute('git config --int --get types.int'))
        .assertEqual('2048');
      expect(
        shell.execute('git config --bool-or-int --get types.bool-or-int')
      ).assertEqual('false');
      expect(
        shell.execute('git config --bool-or-str --get types.bool-or-str')
      ).assertEqual('maybe');
      expect(shell.execute('git config --path --get types.path'))
        .assertEqual('/sandbox/repo');
      expect(shell.execute('git config --expiry-date --get types.expiry'))
        .assertEqual('0');
      expect(shell.execute('git config --type int --get types.int'))
        .assertEqual('2048');
      expect(
        shell.execute(
          'git config --bool --default yes --get missing.value'
        )
      ).assertEqual('true');

      expect(shell.execute('git config --bool types.write yes'))
        .assertEqual('');
      expect(service.lastConfigValue).assertEqual('true');
      expect(shell.execute('git config --int types.write 2k'))
        .assertEqual('');
      expect(service.lastConfigValue).assertEqual('2048');
      expect(shell.execute('git config --path types.write ~/repo'))
        .assertEqual('');
      expect(service.lastConfigValue).assertEqual('~/repo');
    });

    it('formats native reflog entries in newest-first order', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git reflog -n 1')).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
    });

    it('routes reflog selectors, filters and pretty formats', 0, (): void => {
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(service, '/sandbox');

      expect(shell.execute(
        "git reflog show --max-count=5 --skip=2 " +
        "--since='2020-01-01' --until='2022-12-31' " +
        "--date=iso --format='%gD|%gs|%s|%an|%ad' " +
        "'HEAD@{2021-06-01}'"
      )).assertEqual(
        'HEAD@{2023-11-14 22:13:20 +0000}|commit: native test|' +
        'native test|Harmony Test|2023-11-14 22:11:40 +0000'
      );
      expect(service.lastReflogRef).assertEqual('HEAD@{2021-06-01}');
      expect(service.lastReflogMaxCount).assertEqual(5);
      expect(service.lastReflogSkip).assertEqual(2);
      expect(service.lastReflogSince).assertEqual('2020-01-01');
      expect(service.lastReflogUntil).assertEqual('2022-12-31');

      expect(shell.execute(
        "git log -g --oneline --skip=1 --after='2020-01-01' " +
        "--before='2022-12-31' HEAD"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(service.lastReflogRef).assertEqual('HEAD');
      expect(service.lastReflogSkip).assertEqual(1);
      expect(service.lastReflogSince).assertEqual('2020-01-01');
      expect(service.lastReflogUntil).assertEqual('2022-12-31');

      expect(shell.execute(
        "git log --walk-reflogs " +
        "--format='%H%x7c%gD%x7c%gs' HEAD"
      )).assertEqual(
        '1234567890abcdef1234567890abcdef12345678|' +
        'HEAD@{0}|commit: native test'
      );
      expect(shell.lastExitCode()).assertEqual(0);
    });

    it('filters reflog entries with Git log pattern options', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );

      expect(shell.execute(
        "git log -g -F -i --grep-reflog='COMMIT:' --oneline"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(shell.execute(
        "git log -g --grep-reflog='native+' --oneline"
      )).assertEqual('');
      expect(shell.execute(
        "git log -g -E --grep-reflog='native+' --oneline"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(shell.execute(
        "git log -g --author='Harmony Test' " +
        "--committer='Harmony Committer' --format='%an|%cn|%at|%ct'"
      )).assertEqual(
        'Harmony Test|Harmony Committer|1699999900|1700000000'
      );
      expect(shell.execute(
        "git log -g --invert-grep --grep-reflog='missing' --oneline"
      )).assertEqual('');
      expect(shell.execute(
        "git log -g --invert-grep --grep-reflog='native' --oneline"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(shell.execute(
        "git log -g --author='Missing' --invert-grep " +
        "--grep='missing' --oneline"
      )).assertEqual('');
      expect(shell.execute(
        "git log -g --all-match --author='Harmony' " +
        "--author='Missing' --oneline"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(shell.execute(
        "git log -g --all-match --grep-reflog='commit' " +
        "--grep-reflog='missing' --oneline"
      )).assertEqual(
        '1234567 HEAD@{0}: commit: native test'
      );
      expect(shell.execute(
        "git log -g --all-match --grep='native' " +
        "--grep='missing' --oneline"
      )).assertEqual('');
      expect(shell.execute(
        "git log -g -G --grep-reflog='[' --oneline"
      ).startsWith('fatal:')).assertTrue();
      expect(shell.execute(
        "git reflog show --all --date=unix --format='%gD'"
      )).assertEqual(
        'refs/heads/main@{1700000000}\nHEAD@{1700000000}'
      );
      expect(shell.execute(
        "git reflog show --all -n 1 --date=unix --format='%gD'"
      )).assertEqual(
        'refs/heads/main@{1700000000}'
      );
    });

    it('routes native reflog management subcommands and flags', 0, (): void => {
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(service, '/sandbox');
      const zeroObjectId: string = '0'.repeat(40);
      const objectId: string =
        '1234567890abcdef1234567890abcdef12345678';

      expect(shell.execute('git reflog list')).assertEqual(
        'HEAD\nrefs/heads/main'
      );
      expect(shell.execute(
        'git reflog exists refs/heads/main'
      )).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(0);
      expect(shell.execute('git reflog exists main')).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(1);

      expect(shell.execute(
        `git reflog write refs/heads/manual ${zeroObjectId} ` +
        `${objectId} 'manual entry'`
      )).assertEqual('');
      expect(service.lastReflogWriteRef).assertEqual(
        'refs/heads/manual'
      );
      expect(service.lastReflogWriteOldObjectId).assertEqual(
        zeroObjectId
      );
      expect(service.lastReflogWriteNewObjectId).assertEqual(objectId);
      expect(service.lastReflogWriteMessage).assertEqual('manual entry');

      expect(shell.execute(
        'git reflog delete --rewrite --updateref --dry-run ' +
        '--verbose main@{0}'
      )).assertEqual(
        'keep commit (initial): one\nwould prune commit: two'
      );
      expect(service.lastReflogDeleteSelectors[0]).assertEqual(
        'main@{0}'
      );
      expect(service.lastReflogDeleteRewrite).assertTrue();
      expect(service.lastReflogDeleteUpdateRef).assertTrue();
      expect(service.lastReflogDeleteDryRun).assertTrue();
      expect(service.lastReflogDeleteVerbose).assertTrue();

      expect(shell.execute(
        'git reflog drop --all --single-worktree'
      )).assertEqual('');
      expect(service.lastReflogDropAll).assertTrue();
      expect(service.lastReflogDropSingleWorktree).assertTrue();
      expect(shell.execute(
        'git reflog expire --expire=all ' +
        '--expire-unreachable never --rewrite --updateref ' +
        '--stale-fix -n --verbose --all --single-worktree'
      )).assertEqual(
        'keep commit (initial): one\nwould prune commit: two'
      );
      expect(service.lastReflogExpireRefs.length).assertEqual(0);
      expect(service.lastReflogExpire).assertEqual('all');
      expect(service.lastReflogExpireUnreachable).assertEqual('never');
      expect(service.lastReflogExpireRewrite).assertTrue();
      expect(service.lastReflogExpireUpdateRef).assertTrue();
      expect(service.lastReflogExpireStaleFix).assertTrue();
      expect(service.lastReflogExpireDryRun).assertTrue();
      expect(service.lastReflogExpireVerbose).assertTrue();
      expect(service.lastReflogExpireAll).assertTrue();
      expect(service.lastReflogExpireSingleWorktree).assertTrue();
      expect(shell.lastExitCode()).assertEqual(0);
    });

    it('routes native rm and mv options through the repository service', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git rm README.md')).assertEqual(
        'Removed 1 file.'
      );
      expect(shell.execute(
        'git rm --cached -f -r -- src'
      )).assertEqual(
        'Removed 1 file from the index.'
      );
      expect(shell.execute(
        'git mv -f -- README.md docs/README.md'
      )).assertEqual('');
      expect(shell.execute('git mv README.md')).assertEqual(
        'usage: git mv [-f] [--] <source> <destination>'
      );
    });

    it('routes native clean modes, output and path filters', 0, (): void => {
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(service, '/sandbox');

      expect(shell.execute('git clean -nd')).assertEqual(
        'Would remove notes.txt\n' +
        'Would remove build/\n' +
        'Would skip repository nested/'
      );
      expect(service.lastCleanPath).assertEqual(
        '/sandbox/workspace/demo-app'
      );
      expect(service.lastCleanOptions.dryRun).assertTrue();
      expect(service.lastCleanOptions.directories).assertTrue();
      expect(service.lastCleanOptions.force).assertEqual(0);

      expect(shell.execute('git clean -fd')).assertEqual(
        'Removing notes.txt\n' +
        'Removing build/\n' +
        'Skipping repository nested/'
      );
      expect(service.lastCleanOptions.force).assertEqual(1);
      expect(service.lastCleanOptions.dryRun).assertFalse();

      expect(shell.execute('git clean -qfd')).assertEqual('');
      expect(service.lastCleanOptions.quiet).assertTrue();

      expect(shell.execute(
        'git clean -x -nd -e keep.log -- src/*.tmp'
      )).assertEqual(
        'Would remove notes.txt\n' +
        'Would remove build/\n' +
        'Would skip repository nested/'
      );
      expect(service.lastCleanOptions.removeIgnored).assertTrue();
      expect(service.lastCleanOptions.ignoredOnly).assertFalse();
      expect(service.lastCleanOptions.excludes.join(',')).assertEqual(
        'keep.log'
      );
      expect(service.lastCleanOptions.paths.join(',')).assertEqual(
        'src/*.tmp'
      );

      shell.execute('git clean -ndX -- ignored-dir');
      expect(service.lastCleanOptions.removeIgnored).assertFalse();
      expect(service.lastCleanOptions.ignoredOnly).assertTrue();
      expect(service.lastCleanOptions.paths.join(',')).assertEqual(
        'ignored-dir'
      );
      expect(shell.execute('git clean -xX')).assertEqual(
        'fatal: cannot combine -x and -X'
      );
      expect(shell.execute('git clean -i')).assertEqual(
        'fatal: git clean -i requires the PTY-backed shell input stream'
      );
    });

    it('formats native show and tag commands through the repository service', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(
        service,
        '/sandbox'
      );
      expect(shell.execute('git show')).assertEqual('HEAD|false|false');
      expect(shell.execute(
        'git show --stat --oneline HEAD~1'
      )).assertEqual('HEAD~1|true|true');
      expect(shell.execute(
        'git show HEAD -- src'
      )).assertEqual('HEAD|false|false|src');
      expect(shell.execute('git tag')).assertEqual(
        'v1.0.0\nv2.0.0'
      );
      expect(shell.execute('git tag -l "v1.*"')).assertEqual(
        'v1.0.0'
      );
      expect(shell.execute('git tag v2.1.0 HEAD')).assertEqual('');
      expect(shell.execute(
        'git tag -a -f v2.1.0 -m "Harmony PC" -m "release" HEAD'
      )).assertEqual('');
      expect(service.lastTagName).assertEqual('v2.1.0');
      expect(service.lastTagTarget).assertEqual('HEAD');
      expect(service.lastTagForce).assertTrue();
      expect(service.lastTagAnnotated).assertTrue();
      expect(service.lastTagMessage).assertEqual(
        'Harmony PC\n\nrelease'
      );
      expect(shell.execute('git tag -d v1.0.0 v2.0.0')).assertEqual(
        "Deleted tag 'v1.0.0'.\nDeleted tag 'v2.0.0'."
      );
    });

    it('edits, cleans and saves annotated tag messages in the terminal', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(service, '/sandbox');
      expect(shell.execute('git tag -a release HEAD')).assertEqual(
        "Editing tag message for 'release'. Use :wq to save or :q! to cancel."
      );
      expect(shell.editorActive()).assertTrue();
      expect(shell.inputPrompt()).assertEqual('TAG_EDITMSG>');
      expect(shell.execute('Release title')).assertEqual('');
      expect(shell.execute('')).assertEqual('');
      expect(shell.execute('Details line   ')).assertEqual('');
      expect(shell.execute('  # editor comment')).assertEqual('');
      expect(shell.execute(':wq')).assertEqual('');
      expect(shell.editorActive()).assertFalse();
      expect(service.lastTagName).assertEqual('release');
      expect(service.lastTagTarget).assertEqual('HEAD');
      expect(service.lastTagAnnotated).assertTrue();
      expect(service.lastTagMessage).assertEqual(
        'Release title\n\nDetails line'
      );
    });

    it('rejects empty tag edits and cancels without creating a tag', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(service, '/sandbox');
      shell.execute('git tag -a empty');
      shell.execute('# only a comment');
      expect(shell.execute(':wq')).assertEqual('fatal: no tag message?');
      expect(shell.editorActive()).assertFalse();
      expect(service.tagCreateCount).assertEqual(0);

      shell.execute('git tag -a cancelled');
      shell.execute('Do not save');
      expect(shell.execute(':cq')).assertEqual('Tag creation cancelled.');
      expect(shell.editorActive()).assertFalse();
      expect(service.tagCreateCount).assertEqual(0);
    });

    it('loads annotated tag messages from files with cleanup modes', 0, (): void => {
      const shell: GitShell = new GitShell();
      const service: MockGitRepositoryService =
        new MockGitRepositoryService();
      shell.attachRepositoryService(service, '/sandbox');
      expect(shell.execute(
        "printf 'File title  \\n\\n# keep this line  \\n' > TAG_MESSAGE"
      )).assertEqual('');
      expect(shell.execute(
        'git tag --cleanup=whitespace -F TAG_MESSAGE file-release'
      )).assertEqual('');
      expect(service.lastTagName).assertEqual('file-release');
      expect(service.lastTagAnnotated).assertTrue();
      expect(service.lastTagMessage).assertEqual(
        'File title\n\n# keep this line'
      );
    });

    it('routes native ls-files selectors and path filters', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git ls-files')).assertEqual(
        'README.md\nsrc/main.ets'
      );
      expect(shell.execute(
        'git ls-files --modified -- README.md'
      )).assertEqual('README.md');
      expect(shell.execute(
        'git ls-files --others --exclude-standard'
      )).assertEqual('notes.txt');
      expect(shell.execute(
        'git ls-files --others --ignored --exclude-standard'
      )).assertEqual('ignored.log');
      expect(shell.execute(
        'git ls-files --deleted'
      )).assertEqual('deleted.txt');
      expect(shell.execute(
        'git ls-files --stage -- src'
      )).assertEqual(
        '100644 1234567890abcdef1234567890abcdef12345678 0\tsrc/main.ets'
      );
      expect(shell.execute(
        'git ls-files --ignored'
      )).assertEqual(
        'fatal: git ls-files --ignored requires --others'
      );
    });

    it('routes native cat-file inspection and typed reads', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git cat-file -t HEAD')).assertEqual('commit');
      expect(shell.execute('git cat-file -s HEAD')).assertEqual('123');
      expect(shell.execute('git cat-file -e HEAD')).assertEqual('');
      expect(shell.execute('git cat-file -p HEAD')).assertEqual(
        'pretty:HEAD'
      );
      expect(shell.execute('git cat-file blob HEAD:README.md')).assertEqual(
        'blob:HEAD:README.md'
      );
      expect(shell.execute('git cat-file -z HEAD')).assertEqual(
        'error: unknown option `-z`'
      );
    });

    it('routes native hash-object options and file lists', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git hash-object README.md src/main.ets'
      )).assertEqual(
        'blob|false|README.md\nblob|false|src/main.ets'
      );
      expect(shell.execute(
        'git hash-object -w -t commit -- commit.txt'
      )).assertEqual(
        'commit|true|commit.txt'
      );
      expect(shell.execute(
        'git hash-object --type=tag tag.txt'
      )).assertEqual(
        'tag|false|tag.txt'
      );
      expect(shell.execute(
        "printf 'hello\\n' | git hash-object --stdin"
      )).assertEqual(
        'blob|false|hello\n'
      );
      expect(shell.execute(
        "printf 'hello' | git hash-object -w --stdin"
      )).assertEqual(
        'blob|true|hello'
      );
    });

    it('routes native check-ignore modes and path lists', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git check-ignore ignored.log README.md'
      )).assertEqual(
        'ignored.log|false'
      );
      expect(shell.execute(
        'git check-ignore -v --no-index ignored.log'
      )).assertEqual(
        '.gitignore:1:*.log\tignored.log|true'
      );
      expect(shell.execute(
        'git check-ignore -q ignored.log'
      )).assertEqual('');
      expect(shell.execute(
        'git check-ignore -qv ignored.log'
      )).assertEqual(
        'fatal: cannot combine --quiet and --verbose'
      );
      expect(shell.execute(
        "printf 'ignored.log\\nREADME.md\\n' | git check-ignore --stdin"
      )).assertEqual(
        'ignored.log|false'
      );
    });

    it('routes native ls-tree options and path filters', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git ls-tree -rdtl --full-name --full-tree HEAD -- src docs'
      )).assertEqual(
        'HEAD\ntrue\ntrue\ntrue\nfalse\nfalse\ntrue\ntrue\ntrue\nsrc,docs'
      );
      expect(shell.execute(
        'git ls-tree --name-status HEAD src'
      )).assertEqual(
        'HEAD\nfalse\nfalse\nfalse\ntrue\nfalse\nfalse\nfalse\nfalse\nsrc'
      );
      expect(shell.execute(
        'git ls-tree --object-only HEAD'
      )).assertEqual(
        'HEAD\nfalse\nfalse\nfalse\nfalse\ntrue\nfalse\nfalse\nfalse\n'
      );
      expect(shell.execute(
        'git ls-tree --name-only --object-only HEAD'
      )).assertEqual(
        'fatal: git ls-tree cannot combine --name-only and --object-only.'
      );
    });

    it('routes native show-ref filters, verification and formatting', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git show-ref --heads --tags --head -dq --hash=12 main v1.0.0'
      )).assertEqual(
        'true\ntrue\ntrue\ntrue\nfalse\ntrue\ntrue\n12\nmain,v1.0.0'
      );
      expect(shell.execute(
        'git show-ref --verify --abbrev=8 refs/heads/main'
      )).assertEqual(
        'false\nfalse\nfalse\nfalse\ntrue\nfalse\nfalse\n8\nrefs/heads/main'
      );
      expect(shell.execute('git show-ref --verify')).assertEqual(
        'usage: git show-ref --verify [--quiet] [--] <ref>...'
      );
      expect(shell.execute(
        "printf 'refs/heads/main\\nrefs/tags/new\\n' | git show-ref --exclude-existing=refs/tags/"
      )).assertEqual(
        'refs/tags/|refs/heads/main\nrefs/tags/new\n'
      );
      expect(shell.execute(
        'git show-ref --verify --exclude-existing'
      )).assertEqual(
        "fatal: options '--exclude-existing' and '--verify' cannot be used together"
      );
    });

    it('routes native rev-list graph options and revision ranges', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git rev-list --all --parents --reverse --first-parent --no-merges --abbrev-commit --abbrev=12 -n 25 main ^base'
      )).assertEqual(
        'true|false|false|false\ntrue|false|true|true\ntrue|false|true|12|25\nmain,^base'
      );
      expect(shell.execute(
        'git rev-list --branches --tags --remotes --count main...topic'
      )).assertEqual(
        'false|true|true|true\nfalse|true|false|false\nfalse|false|false|7|4294967295\nmain...topic'
      );
      expect(shell.execute(
        'git rev-list HEAD -- src/main.ets docs'
      )).assertEqual(
        'false|false|false|false\nfalse|false|false|false\nfalse|false|false|7|4294967295\nHEAD|src/main.ets,docs'
      );
      expect(shell.execute(
        "printf 'HEAD\\n--\\nsrc/main.ets\\n' | git rev-list --stdin"
      )).assertEqual(
        'false|false|false|false\nfalse|false|false|false\nfalse|false|false|7|4294967295\nHEAD|src/main.ets'
      );
    });

    it('routes native merge-base graph modes', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git merge-base --all --octopus main topic release'
      )).assertEqual(
        'true|true|false\nmain,topic,release'
      );
      expect(shell.execute(
        'git merge-base --independent main topic'
      )).assertEqual(
        'false|false|true\nmain,topic'
      );
      expect(shell.lastExitCode()).assertEqual(0);
      expect(shell.execute(
        'git merge-base main unrelated'
      )).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(1);
      expect(shell.execute(
        'git merge-base --is-ancestor main topic'
      )).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(0);
      expect(shell.execute(
        'git merge-base --is-ancestor topic main'
      )).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(1);
      expect(shell.execute(
        'git merge-base --is-ancestor main'
      )).assertEqual(
        'usage: git merge-base --is-ancestor <commit> <commit>'
      );
      expect(shell.lastExitCode()).assertEqual(129);
      expect(shell.execute(
        'git merge-base --is-ancestor --all main topic'
      )).assertEqual(
        'fatal: git merge-base --is-ancestor cannot be combined with other graph modes'
      );
      expect(shell.lastExitCode()).assertEqual(128);
      expect(shell.execute(
        'git merge-base --fork-point main topic'
      )).assertEqual('fork-point');
      expect(shell.lastExitCode()).assertEqual(0);
      expect(shell.execute(
        'git merge-base --fork-point topic main'
      )).assertEqual('');
      expect(shell.lastExitCode()).assertEqual(1);
      expect(shell.execute(
        'git merge-base --fork-point'
      )).assertEqual(
        'usage: git merge-base --fork-point <ref> [<commit>]'
      );
      expect(shell.lastExitCode()).assertEqual(129);
    });

    it('routes native for-each-ref formatting and graph filters', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git for-each-ref --count=5 --format="%(refname:short) %(subject)" --sort=-committerdate --sort=refname --exclude=refs/tags/archive --points-at=HEAD --merged=main --no-merged=old --contains=base --no-contains=drop --ignore-case --include-root-refs refs/heads'
      )).assertEqual(
        '5|%(refname:short) %(subject)|true|true\n-committerdate,refname\nrefs/heads\nrefs/tags/archive\nHEAD|main|old|base|drop'
      );
      const stdinRefOutput: string = shell.execute(
        "printf 'refs/heads/main\\nrefs/tags/v1.0.0\\n' | git for-each-ref --stdin"
      );
      expect(stdinRefOutput).assertEqual(
        '4294967295||false|false\n\nrefs/heads/main,refs/tags/v1.0.0\n\n||||'
      );
    });

    it('routes native symbolic-ref reads and writes', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git symbolic-ref --short --no-recurse HEAD'
      )).assertEqual('HEAD|true|false');
      expect(shell.execute(
        'git symbolic-ref -m "switch head" HEAD refs/heads/release'
      )).assertEqual('');
      expect(shell.execute(
        'git symbolic-ref --delete refs/meta/current'
      )).assertEqual('');
    });

    it('routes native update-ref compare-and-swap operations', 0, (): void => {
      const shell: GitShell = new GitShell();
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute(
        'git update-ref -m "advance branch" refs/heads/main HEAD HEAD~1'
      )).assertEqual('');
      expect(shell.execute(
        'git update-ref --no-deref HEAD HEAD'
      )).assertEqual('');
      expect(shell.execute(
        'git update-ref -d refs/heads/release HEAD'
      )).assertEqual('');
      expect(shell.execute(
        "printf 'start\\ncreate refs/heads/batch HEAD\\ncommit\\n' | " +
        'git update-ref --stdin --no-deref --create-reflog ' +
        '-m "batch refs"'
      )).assertEqual(
        'true|true|false|false|batch refs|' +
        'start,create refs/heads/batch HEAD,commit,'
      );
      expect(shell.execute('git update-ref --stdin')).assertEqual(
        'false|false|false|false||'
      );
      expect(shell.execute(
        "printf '%s\\0' 'create refs/heads/nul' HEAD | " +
        'git update-ref --stdin -z'
      )).assertEqual(
        'false|false|true|false||create refs/heads/nul<NUL>HEAD<NUL>'
      );
      expect(shell.execute('git update-ref -z HEAD HEAD')).assertEqual(
        'usage: git update-ref [<options>] --stdin [-z]'
      );
      expect(shell.execute(
        'git update-ref --stdin --batch-updates'
      )).assertEqual(
        'false|false|false|true||'
      );
      expect(shell.execute(
        'git update-ref --batch-updates refs/heads/main HEAD'
      )).assertEqual(
        'usage: git update-ref [<options>] --stdin [-z]'
      );
    });
  });
}
