import { describe, expect, it } from '@ohos/hypium';
import { GitShell } from '../main/ets/git/GitShell';

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
  });
}
