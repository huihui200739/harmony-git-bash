import { describe, expect, it } from '@ohos/hypium';
import {
  TerminalCompletion,
  TerminalInputSession
} from '../main/ets/terminal/TerminalInputSession';

export default function terminalInputSessionTest(): void {
  describe('TerminalInputSession', (): void => {
    it('navigates deduplicated history and restores the current draft', 0,
      (): void => {
        const session: TerminalInputSession =
          new TerminalInputSession();
        session.record('git status');
        session.record('git status');
        session.record('git log --oneline');

        expect(session.previous('git diff')).assertEqual(
          'git log --oneline'
        );
        session.edit('git log --oneline');
        expect(session.previous('ignored')).assertEqual('git status');
        session.edit('git status');
        expect(session.previous('ignored')).assertEqual('git status');
        expect(session.next()).assertEqual('git log --oneline');
        session.edit('git log --oneline');
        expect(session.next()).assertEqual('git diff');
        expect(session.entries().length).assertEqual(2);
      });

    it('keeps only the configured number of history entries', 0,
      (): void => {
        const session: TerminalInputSession =
          new TerminalInputSession(2);
        session.record('pwd');
        session.record('git status');
        session.record('git log');

        expect(session.entries().join('|')).assertEqual(
          'git status|git log'
        );
      });

    it('completes shell, Git and reflog commands', 0, (): void => {
      const session: TerminalInputSession =
        new TerminalInputSession();
        expect(session.complete('printen').value).assertEqual('printenv ');
      expect(session.complete('git check-i').value).assertEqual(
        'git check-ignore '
      );
      expect(session.complete('git reflog exp').value).assertEqual(
        'git reflog expire '
      );
    });

    it('uses the longest common prefix and exposes ambiguous matches', 0,
      (): void => {
        const session: TerminalInputSession =
          new TerminalInputSession();
        const completion: TerminalCompletion =
          session.complete('git re');

        expect(completion.value).assertEqual('git re');
        expect(completion.candidates.includes('reflog')).assertTrue();
        expect(completion.candidates.includes('remote')).assertTrue();
        expect(completion.candidates.includes('restore')).assertTrue();
      });

    it('completes current-directory paths without adding a space to folders',
      0, (): void => {
        const session: TerminalInputSession =
          new TerminalInputSession();
        expect(
          session.complete(
            'cat READ',
            ['README.md', 'src/', 'docs/']
          ).value
        ).assertEqual('cat README.md ');
        expect(
          session.complete(
            'cd sr',
            ['README.md', 'src/', 'docs/']
          ).value
        ).assertEqual('cd src/');
      });
  });
}
