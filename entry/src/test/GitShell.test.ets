import { describe, expect, it } from '@ohos/hypium';
import {
  GitRepositoryService,
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

    it('uses real directory navigation and refuses unfinished writes', 0, (): void => {
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
      expect(shell.execute('git add README.md').includes(
        'write/object operation is not enabled'
      )).assertTrue();
      expect(shell.execute('git open /external/repository')).assertEqual(
        'Opened Git repository: /external/repository'
      );
      expect(shell.execute('git open /missing').startsWith('fatal:')).assertTrue();
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
  });
}
