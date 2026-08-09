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
  RepositoryRevListOptions,
  RepositoryShowRefOptions,
  RepositorySnapshot
} from '../main/ets/git/GitRepositoryService';
import { GitShell } from '../main/ets/git/GitShell';

class MockGitRepositoryService implements GitRepositoryService {
  private readonly demoPath: string = '/sandbox/workspace/demo-app';
  private readonly externalPath: string = '/external/repository';
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
    return this.snapshot(path);
  }

  directoryExists(path: string): boolean {
    return path === '/sandbox' ||
      path === '/sandbox/workspace' ||
      path === this.demoPath ||
      path === `${this.demoPath}/src` ||
      path === '/sandbox/empty' ||
      path === this.externalPath;
  }

  listDirectory(path: string): string[] {
    if (!this.directoryExists(path)) {
      throw new Error('No such directory');
    }
    return ['.git/', 'README.md', 'src/'];
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
    _name: string,
    _startPoint: string
  ): RepositoryOperation {
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
    return `${name}|${shortName}|${recurse}`;
  }

  updateSymbolicRef(
    path: string,
    _name: string,
    _target: string,
    _deleteReference: boolean,
    _message: string
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  updateRef(
    path: string,
    _name: string,
    _newValue: string,
    _oldValue: string,
    _deleteReference: boolean,
    _noDeref: boolean,
    _message: string,
    _createReflog: boolean
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  updateRefs(
    path: string,
    input: string,
    noDeref: boolean,
    createReflog: boolean,
    message: string
  ): RepositoryOperation {
    const operation: RepositoryOperation = this.operation(path, 2);
    operation.output = [
      `${noDeref}|${createReflog}|${message}|` +
      input.replace(/\n/g, ',')
    ];
    return operation;
  }

  createTag(
    path: string,
    _name: string,
    _target: string,
    _force: boolean,
    _annotated: boolean,
    _message: string
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  deleteTags(path: string, names: string[]): RepositoryOperation {
    return this.operation(path, names.length);
  }

  readConfig(_path: string): RepositoryConfigEntry[] {
    return [
      { key: 'user.name', value: 'Harmony Test' },
      { key: 'user.email', value: 'harmony@example.invalid' },
      { key: 'remote.origin.url', value: 'https://example.invalid/repository.git' }
    ];
  }

  setConfig(
    path: string,
    _key: string,
    _value: string
  ): RepositoryOperation {
    return this.operation(path, 1);
  }

  unsetConfig(path: string, _key: string): RepositoryOperation {
    return this.operation(path, 1);
  }

  addRemote(
    path: string,
    _name: string,
    _url: string
  ): RepositoryOperation {
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

  reflog(
    _path: string,
    _ref: string,
    _maxCount: number
  ): RepositoryReflogEntry[] {
    return [{
      oldId: '0000000000000000000000000000000000000000',
      newId: '1234567890abcdef1234567890abcdef12345678',
      actor: 'Harmony Test <harmony@example.invalid>',
      timestamp: '1700000000 +0000',
      message: 'commit: native test'
    }];
  }

  private snapshot(repositoryPath: string): RepositorySnapshot {
    return {
      valid: true,
      repositoryPath,
      gitDirectory: `${repositoryPath}/.git`,
      branch: 'main',
      head: '1234567890abcdef1234567890abcdef12345678',
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
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
        '/sandbox'
      );
      expect(shell.execute('git config --list')).assertEqual(
        'user.name\tHarmony Test\n' +
        'user.email\tharmony@example.invalid\n' +
        'remote.origin.url\thttps://example.invalid/repository.git'
      );
      expect(shell.execute('git config --get user.name')).assertEqual(
        'Harmony Test'
      );
      expect(shell.execute('git config user.name "Updated Test"')).assertEqual('');
      expect(shell.execute('git config --unset core.editor')).assertEqual('');
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
      shell.attachRepositoryService(
        new MockGitRepositoryService(),
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
        'git tag -a v2.1.0 -m "Harmony PC release" HEAD'
      )).assertEqual('');
      expect(shell.execute('git tag -d v1.0.0 v2.0.0')).assertEqual(
        "Deleted tag 'v1.0.0'.\nDeleted tag 'v2.0.0'."
      );
      expect(shell.execute('git tag -a missing-message')).assertEqual(
        'fatal: annotated tags require -m because editor integration is not available yet'
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
        'true|true|batch refs|' +
        'start,create refs/heads/batch HEAD,commit,'
      );
      expect(shell.execute('git update-ref --stdin')).assertEqual(
        'false|false||'
      );
      expect(shell.execute('git update-ref --stdin -z')).assertEqual(
        'fatal: git update-ref -z input is not supported yet'
      );
      expect(shell.execute(
        'git update-ref --stdin --batch-updates'
      )).assertEqual(
        'fatal: git update-ref --batch-updates is not supported yet'
      );
    });
  });
}
