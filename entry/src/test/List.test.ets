import gitShellTest from './GitShell.test';
import terminalInputSessionTest from './TerminalInputSession.test';
import credentialSecurityTest from './CredentialSecurity.test';

export default function testsuite(): void {
  gitShellTest();
  terminalInputSessionTest();
  credentialSecurityTest();
}
