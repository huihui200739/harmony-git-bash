import gitShellTest from './GitShell.test';
import terminalInputSessionTest from './TerminalInputSession.test';

export default function testsuite(): void {
  gitShellTest();
  terminalInputSessionTest();
}
