import { describe, expect, it } from '@ohos/hypium';
import {
  GitRepositoryService,
  RepositoryConfigEntry,
  RepositoryCommit,
  RepositoryOperation,
  RepositoryReflogEntry,
  RepositorySnapshot
} from '../main/ets/git/GitRepositoryService';
import { GitShell } from '../main/ets/git/GitShell';

class MockGitRepositoryService implements GitRepositoryService {
  private readonly demoPath: string = '/sandbox/workspace/demo-app';
  private readonly externalPath: string = '/external/repository';

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
    oneLine: boolean
  ): string {
    return `${revision.length === 0 ? 'HEAD' : revision}|${statOnly}|${oneLine}`;
  }

  tags(_path: string): string[] {
    return ['v1.0.0', 'v2.0.0'];
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
      expect(shell.execute('git tag')).assertEqual(
        'v1.0.0\nv2.0.0'
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
  });
}
