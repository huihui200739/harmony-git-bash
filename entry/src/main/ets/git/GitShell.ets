import {
  GitRepositoryService,
  RepositoryConfigEntry,
  RepositoryCredentials,
  RepositoryCleanOptions,
  RepositoryCleanResult,
  RepositoryFileStatus,
  RepositoryForEachRefOptions,
  RepositoryListFilesOptions,
  RepositoryListTreeOptions,
  RepositoryMergeBaseOptions,
  RepositoryRemote,
  RepositoryReflogEntry,
  RepositoryOperation,
  RepositoryRevListOptions,
  RepositoryRemoteAdvertisement,
  RepositoryRemoteReference,
  RepositoryRemotePushUpdate,
  RepositoryShowRefOptions,
  RepositorySnapshot
} from './GitRepositoryService';

export interface GitFileChange {
  path: string;
  state: string;
  staged: boolean;
}

export interface GitCommit {
  id: string;
  subject: string;
  author: string;
  timestamp: string;
}

export interface GitShellSnapshot {
  currentPath: string;
  branch: string;
  files: GitFileChange[];
  commits: GitCommit[];
}

interface ShellRedirection {
  commandTokens: string[];
  inputPath: string;
  outputPath: string;
  append: boolean;
  error: string;
}

interface ShellAssignment {
  name: string;
  value: string;
}

interface ShellVariable {
  name: string;
  endIndex: number;
}

interface ReflogCommandOptions {
  maxCount: number;
  skip: number;
  since: string;
  until: string;
  ref: string;
  format: string;
  pretty: string;
  date: string;
  abbrevCommit: boolean;
  grepReflog: string[];
  grep: string[];
  authors: string[];
  committers: string[];
  allMatch: boolean;
  invertGrep: boolean;
  ignoreCase: boolean;
  fixedStrings: boolean;
  extendedRegexp: boolean;
  all: boolean;
  singleWorktree: boolean;
}

interface ShellFileReadResult {
  success: boolean;
  content: string;
  error: string;
}

interface ShellFileWriteResult {
  success: boolean;
  error: string;
}

interface GitInvocation {
  args: string[];
  config: RepositoryConfigEntry[];
  error: string;
}

interface TagEditorSession {
  name: string;
  target: string;
  force: boolean;
  cleanup: string;
  lines: string[];
}

interface CredentialPromptSession {
  rawCommand: string;
  url: string;
  phase: string;
  username: string;
}

export class GitShell {
  private currentPath: string = '/home/harmony/workspace/demo-app';
  private homePath: string = '/home/harmony';
  private branch: string = 'main';
  private lastCommandExitCode: number = 0;
  private lastExpansionExitCode: number = 0;
  private repositoryService?: GitRepositoryService;
  private tagEditor?: TagEditorSession;
  private credentialPrompt?: CredentialPromptSession;
  private lastNetworkResponseCode: number = 0;
  private lastNetworkRemoteUrl: string = '';
  private readonly environment: Record<string, string | undefined> = {};
  private readonly redirectedFiles: Record<string, string> = {};
  private readonly branches: string[] = ['main'];
  private readonly remotes: string[] = ['origin\thttps://github.com/huihui200739/demo-app.git (fetch)', 'origin\thttps://github.com/huihui200739/demo-app.git (push)'];
  private readonly files: GitFileChange[] = [
    { path: 'src/main/ets/pages/Index.ets', state: 'modified', staged: false },
    { path: 'README.md', state: 'modified', staged: false },
    { path: 'docs/porting-notes.md', state: 'untracked', staged: false }
  ];
  private readonly commits: GitCommit[] = [
    { id: 'a8c4e21', subject: 'Initial HarmonyOS PC adaptation', author: 'Harmony Developer', timestamp: '2 hours ago' },
    { id: 'd5f8b14', subject: 'Import Git Bash compatibility baseline', author: 'Harmony Developer', timestamp: '1 day ago' },
    { id: 'cf5497b', subject: 'git-for-windows upstream snapshot', author: 'git-for-windows', timestamp: '1 day ago' }
  ];

  constructor() {
    this.setEnvironment('HOME', this.homePath);
    this.setEnvironment('PWD', this.currentPath);
    this.setEnvironment('OLDPWD', '');
    this.setEnvironment('PATH', '/usr/local/bin:/usr/bin:/bin');
    this.setEnvironment('SHELL', '/bin/bash');
    this.setEnvironment('TERM', 'xterm-256color');
    this.setEnvironment('USER', 'harmony');
  }

  attachRepositoryService(
    repositoryService: GitRepositoryService,
    homePath: string
  ): string {
    this.repositoryService = repositoryService;
    this.homePath = homePath;
    this.setEnvironment('HOME', homePath);
    const demoPath: string = `${homePath}/workspace/demo-app`;
    const snapshot: RepositorySnapshot =
      repositoryService.initialize(demoPath, true);
    if (!snapshot.valid) {
      this.setCurrentPath(homePath);
      return `fatal: native Git service initialization failed: ${snapshot.error}`;
    }
    this.setCurrentPath(snapshot.repositoryPath);
    return `Native repository ready: ${snapshot.repositoryPath}`;
  }

  openRepository(path: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const snapshot: RepositorySnapshot =
      this.repositoryService.inspect(path);
    if (!snapshot.valid) {
      return `fatal: ${snapshot.error}`;
    }
    this.setCurrentPath(snapshot.repositoryPath);
    return `Opened Git repository: ${snapshot.repositoryPath}`;
  }

  displayPath(): string {
    if (this.currentPath === this.homePath ||
        this.currentPath.startsWith(`${this.homePath}/`)) {
      return this.currentPath.replace(this.homePath, '~');
    }
    return this.currentPath;
  }

  prompt(): string {
    const snapshot: RepositorySnapshot | undefined =
      this.currentRepository();
    const activeBranch: string =
      snapshot !== undefined && snapshot.valid
        ? snapshot.branch
        : this.branch;
    return `harmony@PC MINGW64 ${this.displayPath()} (${activeBranch})`;
  }

  editorActive(): boolean {
    return this.tagEditor !== undefined;
  }

  credentialPromptActive(): boolean {
    return this.credentialPrompt !== undefined;
  }

  credentialPasswordActive(): boolean {
    return this.credentialPrompt?.phase === 'password';
  }

  shouldEchoInput(): boolean {
    return !this.credentialPasswordActive();
  }

  inputPrompt(): string {
    if (this.editorActive()) {
      return 'TAG_EDITMSG>';
    }
    if (this.credentialPrompt !== undefined) {
      const label: string = this.credentialPrompt.phase === 'password'
        ? 'Password'
        : 'Username';
      return `${label} for '${this.credentialPrompt.url}':`;
    }
    return `${this.prompt()}$`;
  }

  snapshot(): GitShellSnapshot {
    const nativeSnapshot: RepositorySnapshot | undefined =
      this.currentRepository();
    if (nativeSnapshot !== undefined && nativeSnapshot.valid) {
      return {
        currentPath: this.currentPath,
        branch: nativeSnapshot.branch,
        files: nativeSnapshot.files.map(
          (file: RepositoryFileStatus): GitFileChange => ({
            path: file.path,
            state: file.tracked ? this.nativeFileState(file) : 'untracked',
            staged: file.staged
          })
        ),
        commits: []
      };
    }
    return {
      currentPath: this.currentPath,
      branch: this.branch,
      files: this.files.map((file: GitFileChange): GitFileChange => ({
        path: file.path,
        state: file.state,
        staged: file.staged
      })),
      commits: this.commits.map((commit: GitCommit): GitCommit => ({
        id: commit.id,
        subject: commit.subject,
        author: commit.author,
        timestamp: commit.timestamp
      }))
    };
  }

  lastExitCode(): number {
    return this.lastCommandExitCode;
  }

  completionPaths(rawCommand: string): string[] {
    const tokenStart: number = Math.max(
      rawCommand.lastIndexOf(' '),
      rawCommand.lastIndexOf('\t'),
      rawCommand.lastIndexOf('|')
    ) + 1;
    const fragment: string = rawCommand.slice(tokenStart);
    if (fragment.startsWith('-') ||
        fragment.includes('"') ||
        fragment.includes("'")) {
      return [];
    }
    const slash: number = fragment.lastIndexOf('/');
    const directoryPrefix: string =
      slash < 0 ? '' : fragment.slice(0, slash + 1);
    const directory: string =
      slash < 0
        ? this.currentPath
        : directoryPrefix === '/'
          ? '/'
          : this.resolvePath(
            directoryPrefix.slice(0, directoryPrefix.length - 1)
          );
    try {
      const entries: string[] = this.repositoryService === undefined
        ? ['.git/', 'README.md', 'docs/', 'src/']
        : this.repositoryService.listDirectory(directory);
      return entries.map(
        (entry: string): string => `${directoryPrefix}${entry}`
      );
    } catch (_error) {
      return [];
    }
  }

  execute(rawCommand: string): string {
    if (this.tagEditor !== undefined) {
      return this.editTagMessage(rawCommand);
    }
    if (this.credentialPrompt !== undefined) {
      this.lastCommandExitCode = 0;
      return `fatal: enter credentials through the interactive terminal prompt.`;
    }
    const previousExitCode: number = this.lastCommandExitCode;
    this.lastCommandExitCode = 0;
    this.lastExpansionExitCode = previousExitCode;
    const command: string = rawCommand.trim();
    if (command.length === 0) {
      return '';
    }
    const pipeline: string[] = this.splitPipeline(command);
    if (pipeline.some((stage: string): boolean => stage.length === 0)) {
      this.lastCommandExitCode = 2;
      return 'bash: syntax error near unexpected token `|\'';
    }
    let input: string = '';
    for (const stage of pipeline) {
      const rawTokens: string[] = this.tokenize(stage, true, false);
      const rawRedirection: ShellRedirection =
        this.extractRedirections(rawTokens);
      if (rawRedirection.error.length > 0) {
        this.lastCommandExitCode = 2;
        return `bash: ${rawRedirection.error}`;
      }
      const assignments: ShellAssignment[] =
        this.leadingAssignments(rawRedirection.commandTokens);
      const savedValues: Record<string, string> = {};
      const wasSet: Record<string, boolean> = {};
      for (const assignment of assignments) {
        wasSet[assignment.name] =
          this.environment[assignment.name] !== undefined;
        savedValues[assignment.name] =
          this.environment[assignment.name] ?? '';
        this.setEnvironment(
          assignment.name,
          this.expandVariablesInToken(assignment.value)
        );
      }

      const parsed: ShellRedirection =
        this.extractRedirections(this.tokenize(stage, true, true));
      if (parsed.error.length > 0) {
        this.lastCommandExitCode = 2;
        this.restoreAssignments(assignments, savedValues, wasSet);
        return `bash: ${parsed.error}`;
      }
      const commandAssignments: ShellAssignment[] =
        this.leadingAssignments(parsed.commandTokens);
      const commandTokens: string[] =
        parsed.commandTokens.slice(commandAssignments.length);
      let stageInput: string = input;
      if (parsed.inputPath.length > 0) {
        const file = this.readShellFile(parsed.inputPath);
        if (!file.success) {
          this.lastCommandExitCode = 1;
          this.restoreAssignments(assignments, savedValues, wasSet);
          return `bash: ${parsed.inputPath}: ${file.error}`;
        }
        stageInput = file.content;
      }
      input = commandTokens.length === 0
        ? ''
        : this.executeCommand(commandTokens, stageInput);
      this.lastExpansionExitCode = this.lastCommandExitCode;

      if (parsed.outputPath.length > 0) {
        const write = this.writeShellFile(
          parsed.outputPath,
          input,
          parsed.append
        );
        if (!write.success) {
          this.lastCommandExitCode = 1;
          this.restoreAssignments(assignments, savedValues, wasSet);
          return `bash: ${parsed.outputPath}: ${write.error}`;
        }
        input = '';
      }

      if (commandTokens.length > 0) {
        this.restoreAssignments(assignments, savedValues, wasSet);
      }
    }
    return input;
  }

  async executeAsync(rawCommand: string): Promise<string> {
    if (this.tagEditor !== undefined) {
      return this.execute(rawCommand);
    }
    if (this.credentialPrompt !== undefined) {
      return this.submitCredentialInput(rawCommand);
    }
    return this.executeNetworkCommand(rawCommand, undefined, true);
  }

  private async executeNetworkCommand(
    rawCommand: string,
    credentials: RepositoryCredentials | undefined,
    promptOnAuthentication: boolean
  ): Promise<string> {
    const command: string = rawCommand.trim();
    const pipeline: string[] = this.splitPipeline(command);
    if (pipeline.length !== 1) {
      return this.execute(rawCommand);
    }
    const parsed: ShellRedirection =
      this.extractRedirections(this.tokenize(command, true, true));
    if (parsed.error.length > 0 ||
        parsed.inputPath.length > 0 ||
        parsed.outputPath.length > 0 ||
        parsed.commandTokens[0] !== 'git') {
      return this.execute(rawCommand);
    }
    const invocation: GitInvocation =
      this.parseGitInvocation(parsed.commandTokens.slice(1));
    if (invocation.error.length > 0 ||
        !['clone', 'fetch', 'pull', 'push', 'ls-remote'].includes(
          invocation.args[0] ?? ''
        )) {
      return this.execute(rawCommand);
    }
    const previousExitCode: number = this.lastCommandExitCode;
    this.lastCommandExitCode = 0;
    this.lastExpansionExitCode = previousExitCode;
    this.lastNetworkResponseCode = 0;
    this.lastNetworkRemoteUrl = '';
    try {
      this.repositoryService?.setCommandConfig(invocation.config);
      const output: string = await this.nativeNetworkCommand(
        invocation.args[0],
        invocation.args.slice(1),
        credentials
      );
      if (this.lastNetworkResponseCode === 401) {
        if (!promptOnAuthentication) {
          this.lastCommandExitCode = 128;
          return `fatal: Authentication failed for '${this.redactRemoteUrl(this.lastNetworkRemoteUrl)}'.`;
        }
        this.credentialPrompt = {
          rawCommand: rawCommand,
          url: this.redactRemoteUrl(this.lastNetworkRemoteUrl),
          phase: 'username',
          username: ''
        };
        return '';
      }
      return output;
    } catch (error) {
      this.lastCommandExitCode = 128;
      return `fatal: ${this.errorMessage(error)}`;
    } finally {
      try {
        this.repositoryService?.setCommandConfig([]);
      } catch (_error) {
      }
    }
  }

  private async submitCredentialInput(rawInput: string): Promise<string> {
    const session: CredentialPromptSession | undefined =
      this.credentialPrompt;
    if (session === undefined) {
      return '';
    }
    if (session.phase === 'username') {
      session.username = rawInput;
      session.phase = 'password';
      return '';
    }
    const credentials: RepositoryCredentials = {
      username: session.username,
      password: rawInput
    };
    this.credentialPrompt = undefined;
    return this.executeNetworkCommand(
      session.rawCommand,
      credentials,
      false
    );
  }

  private executeCommand(tokens: string[], input: string): string {
    const verb: string = tokens[0] ?? '';
    if (verb.length === 0) {
      return '';
    }
    if (verb === 'clear') {
      return '__CLEAR__';
    }
    if (verb === 'help') {
      return this.helpText();
    }
    if (verb === 'pwd') {
      return this.currentPath;
    }
    if (verb === 'ls' || verb === 'dir') {
      if (this.repositoryService !== undefined) {
        try {
          return this.repositoryService.listDirectory(this.currentPath).join('  ');
        } catch (error) {
          this.lastCommandExitCode = 1;
          return `bash: ${verb}: ${this.errorMessage(error)}`;
        }
      }
      return 'README.md  docs  src  .git';
    }
    if (verb === 'cd') {
      return this.changeDirectory(tokens.slice(1));
    }
    if (verb === 'echo') {
      return this.echo(tokens.slice(1));
    }
    if (verb === 'printf') {
      return this.printf(tokens.slice(1));
    }
    if (verb === 'cat') {
      return this.cat(tokens.slice(1), input);
    }
    if (verb === 'env') {
      return this.environmentCommand(tokens.slice(1), input);
    }
    if (verb === 'printenv') {
      return this.printEnvironment(tokens.slice(1));
    }
    if (verb === 'export') {
      return this.exportEnvironment(tokens.slice(1));
    }
    if (verb === 'unset') {
      return this.unsetEnvironmentCommand(tokens.slice(1));
    }
    if (verb === 'set') {
      return this.environmentText();
    }
    if (verb !== 'git') {
      this.lastCommandExitCode = 127;
      return `bash: ${verb}: command not found\nType 'help' to see the available Harmony Git Bash commands.`;
    }
    return this.executeGit(tokens.slice(1), input);
  }

  private executeGit(args: string[], input: string): string {
    const invocation: GitInvocation = this.parseGitInvocation(args);
    if (invocation.error.length > 0) {
      this.lastCommandExitCode = 129;
      return invocation.error;
    }
    try {
      this.repositoryService?.setCommandConfig(invocation.config);
      return this.executeGitCommand(invocation.args, input);
    } catch (error) {
      this.lastCommandExitCode = 128;
      return `fatal: ${this.errorMessage(error)}`;
    } finally {
      try {
        this.repositoryService?.setCommandConfig([]);
      } catch (_error) {
      }
    }
  }

  private executeGitCommand(args: string[], input: string): string {
    if (args.length === 0) {
      return 'usage: git <command> [<args>]\n\nType \'git help\' for an overview.';
    }
    const subcommand: string = args[0];
    const nativeSnapshot: RepositorySnapshot | undefined =
      this.currentRepository();
    if (subcommand === 'help') {
      return this.helpText();
    }
    if (subcommand === 'status') {
      if (this.repositoryService !== undefined) {
        return this.nativeStatus(
          nativeSnapshot,
          args.includes('--short') || args.includes('-s')
        );
      }
      return this.status(args.includes('--short') || args.includes('-s'));
    }
    if (subcommand === 'open') {
      const path: string = args[1] ?? '';
      return path.length === 0
        ? 'usage: git open [<repository-path>]\nRun "git open" without a path in the app to choose a folder.'
        : this.openRepository(path);
    }
    if (subcommand === 'rev-parse' && this.repositoryService !== undefined) {
      return this.nativeRevParse(nativeSnapshot, args.slice(1));
    }
    if (subcommand === 'init' && this.repositoryService !== undefined) {
      const before: RepositorySnapshot | undefined = nativeSnapshot;
      const targetPath: string =
        before !== undefined && before.valid
          ? before.repositoryPath
          : this.currentPath;
      const initialized: RepositorySnapshot =
        this.repositoryService.initialize(targetPath, false);
      if (!initialized.valid) {
        return `fatal: ${initialized.error}`;
      }
      return `${before !== undefined && before.valid ? 'Reinitialized' : 'Initialized'} empty Git repository in ${initialized.gitDirectory}/`;
    }
    if (this.repositoryService !== undefined) {
      if (subcommand === 'ls-remote') {
        return this.nativeListRemote(args.slice(1));
      }
      if (subcommand === 'fetch' || subcommand === 'pull' ||
          subcommand === 'push' || subcommand === 'clone') {
        if (subcommand === 'fetch') {
          return 'git fetch: run this command from the interactive terminal so NetworkKit can complete asynchronously.';
        }
        return `git ${subcommand}: run this command from the interactive terminal so NetworkKit can complete asynchronously.`;
      }
      if (nativeSnapshot === undefined || !nativeSnapshot.valid) {
        return `fatal: not a git repository (or any parent directories): .git`;
      }
      if (subcommand === 'branch') {
        return this.nativeBranch(nativeSnapshot, args.slice(1));
      }
      if (subcommand === 'remote') {
        return this.nativeRemote(nativeSnapshot, args.slice(1));
      }
      if (subcommand === 'add') {
        return this.nativeStage(args.slice(1));
      }
      if (subcommand === 'rm') {
        return this.nativeRemove(args.slice(1));
      }
      if (subcommand === 'clean') {
        return this.nativeClean(args.slice(1));
      }
      if (subcommand === 'mv') {
        return this.nativeMove(args.slice(1));
      }
      if (subcommand === 'restore') {
        return this.nativeRestore(args.slice(1));
      }
      if (subcommand === 'reset') {
        return this.nativeReset(args.slice(1));
      }
      if (subcommand === 'commit') {
        return this.nativeCommit(args.slice(1));
      }
      if (subcommand === 'switch' || subcommand === 'checkout') {
        return this.nativeSwitch(args.slice(1));
      }
      if (subcommand === 'diff') {
        return this.nativeDiff(args.slice(1));
      }
      if (subcommand === 'log') {
        return this.nativeLog(args.slice(1));
      }
      if (subcommand === 'show') {
        return this.nativeShow(args.slice(1));
      }
      if (subcommand === 'ls-files') {
        return this.nativeListFiles(args.slice(1));
      }
      if (subcommand === 'hash-object') {
        return this.nativeHashObject(args.slice(1), input);
      }
      if (subcommand === 'check-ignore') {
        return this.nativeCheckIgnore(args.slice(1), input);
      }
      if (subcommand === 'cat-file') {
        return this.nativeCatFile(args.slice(1));
      }
      if (subcommand === 'ls-tree') {
        return this.nativeListTree(args.slice(1));
      }
      if (subcommand === 'show-ref') {
        return this.nativeShowRef(args.slice(1), input);
      }
      if (subcommand === 'rev-list') {
        return this.nativeRevList(args.slice(1), input);
      }
      if (subcommand === 'merge-base') {
        return this.nativeMergeBase(args.slice(1));
      }
      if (subcommand === 'for-each-ref') {
        return this.nativeForEachRef(args.slice(1), input);
      }
      if (subcommand === 'symbolic-ref') {
        return this.nativeSymbolicRef(args.slice(1));
      }
      if (subcommand === 'update-ref') {
        return this.nativeUpdateRef(args.slice(1), input);
      }
      if (subcommand === 'tag') {
        return this.nativeTag(args.slice(1));
      }
      if (subcommand === 'config') {
        return this.nativeConfig(args.slice(1));
      }
      if (subcommand === 'reflog') {
        return this.nativeReflog(args.slice(1));
      }
      return `git: '${subcommand}' is not a supported Harmony Git Bash command yet.\nSupported native commands: open, status, add, rm, mv, clean, restore, reset, commit, diff, log, show, cat-file, hash-object, ls-tree, ls-files, check-ignore, show-ref, rev-list, merge-base, for-each-ref, symbolic-ref, update-ref, tag, reflog, config, branch, switch, checkout, remote, ls-remote, rev-parse, init.`;
    }
    if (subcommand === 'add') {
      return this.stage(args.slice(1));
    }
    if (subcommand === 'restore') {
      return this.restore(args.slice(1));
    }
    if (subcommand === 'reset') {
      return this.reset(args.slice(1));
    }
    if (subcommand === 'commit') {
      return this.commit(args.slice(1));
    }
    if (subcommand === 'log') {
      return this.log(args);
    }
    if (subcommand === 'branch') {
      return this.branchCommand(args.slice(1));
    }
    if (subcommand === 'switch' || subcommand === 'checkout') {
      return this.switchBranch(args.slice(1));
    }
    if (subcommand === 'diff') {
      return this.diff(args.includes('--staged') || args.includes('--cached'));
    }
    if (subcommand === 'remote') {
      return this.remote(args.slice(1));
    }
    if (subcommand === 'init') {
      return 'Reinitialized existing Git repository in /home/harmony/workspace/demo-app/.git/';
    }
    if (subcommand === 'fetch' || subcommand === 'pull' ||
        subcommand === 'push' || subcommand === 'clone') {
      return `git ${subcommand}: network transport will be handled by the Harmony native Git service.\nThe terminal contract is ready; libgit2/Git transport binding is the next implementation milestone.`;
    }
    return `git: '${subcommand}' is not a supported Harmony Git Bash command yet.\nSupported: status, add, rm, clean, restore, reset, commit, log, branch, switch, checkout, diff, remote, init.`;
  }

  private status(short: boolean): string {
    const staged: GitFileChange[] = this.files.filter((file: GitFileChange): boolean => file.staged);
    const unstaged: GitFileChange[] = this.files.filter((file: GitFileChange): boolean => !file.staged && file.state !== 'untracked');
    const untracked: GitFileChange[] = this.files.filter((file: GitFileChange): boolean => !file.staged && file.state === 'untracked');
    if (short) {
      const lines: string[] = [];
      for (const file of this.files) {
        if (file.staged) {
          lines.push(`M  ${file.path}`);
        } else if (file.state === 'untracked') {
          lines.push(`?? ${file.path}`);
        } else {
          lines.push(` M ${file.path}`);
        }
      }
      return lines.join('\n');
    }
    let output: string = `On branch ${this.branch}\nYour branch is up to date with 'origin/${this.branch}'.\n`;
    if (staged.length === 0 && unstaged.length === 0 && untracked.length === 0) {
      return `${output}\nnothing to commit, working tree clean`;
    }
    if (staged.length > 0) {
      output += '\nChanges to be committed:\n  (use "git restore --staged <file>..." to unstage)\n';
      for (const file of staged) {
        output += `\tmodified:   ${file.path}\n`;
      }
    }
    if (unstaged.length > 0) {
      output += '\nChanges not staged for commit:\n  (use "git add <file>..." to update what will be committed)\n';
      for (const file of unstaged) {
        output += `\tmodified:   ${file.path}\n`;
      }
    }
    if (untracked.length > 0) {
      output += '\nUntracked files:\n  (use "git add <file>..." to include in what will be committed)\n';
      for (const file of untracked) {
        output += `\t${file.path}\n`;
      }
    }
    return output.trimEnd();
  }

  private stage(paths: string[]): string {
    if (paths.length === 0) {
      return 'Nothing specified, nothing added.\nMaybe you wanted to say \'git add .\'?';
    }
    const all: boolean = paths.includes('.') || paths.includes('-A') || paths.includes('--all');
    const requested: string[] = paths.filter((path: string): boolean => !path.startsWith('-'));
    let staged: number = 0;
    for (const file of this.files) {
      if (all || requested.includes(file.path)) {
        if (!file.staged) {
          file.staged = true;
          staged++;
        }
      }
    }
    if (staged === 0 && !all) {
      return `fatal: pathspec '${requested[0]}' did not match any files`;
    }
    return staged === 0 ? '' : `Staged ${staged} file${staged === 1 ? '' : 's'}.`;
  }

  private restore(args: string[]): string {
    const staged: boolean = args.includes('--staged');
    const paths: string[] = args.filter((value: string): boolean => !value.startsWith('-'));
    if (!staged) {
      return 'Working tree restore is connected to the native file service and is not enabled in the in-memory compatibility session.';
    }
    let changed: number = 0;
    for (const file of this.files) {
      if ((paths.length === 0 || paths.includes(file.path) || paths.includes('.')) && file.staged) {
        file.staged = false;
        changed++;
      }
    }
    return changed === 0 ? '' : `Unstaged ${changed} file${changed === 1 ? '' : 's'}.`;
  }

  private reset(args: string[]): string {
    if (args.includes('--hard')) {
      return 'git reset --hard requires the native file service and is intentionally unavailable in the compatibility session.';
    }
    return this.restore(['--staged'].concat(args));
  }

  private commit(args: string[]): string {
    const messageIndex: number = args.findIndex((value: string): boolean => value === '-m' || value === '--message');
    const message: string = messageIndex >= 0 ? args[messageIndex + 1] ?? '' : '';
    const staged: GitFileChange[] = this.files.filter((file: GitFileChange): boolean => file.staged);
    if (message.length === 0) {
      return 'Aborting commit due to empty commit message.';
    }
    if (staged.length === 0) {
      return 'On branch ' + this.branch + '\nnothing to commit, working tree clean';
    }
    const id: string = this.commitId(message);
    this.commits.unshift({ id, subject: message, author: 'Harmony Developer', timestamp: 'now' });
    const names: string[] = staged.map((file: GitFileChange): string => file.path);
    for (let index: number = this.files.length - 1; index >= 0; index--) {
      if (this.files[index].staged) {
        this.files.splice(index, 1);
      }
    }
    return `[${this.branch} ${id}] ${message}\n ${names.length} file${names.length === 1 ? '' : 's'} changed`;
  }

  private log(args: string[]): string {
    const oneLine: boolean = args.includes('--oneline');
    const maxIndex: number = args.findIndex((value: string): boolean => value === '-n' || value === '--max-count');
    const requestedCount: number = maxIndex >= 0 ? Number(args[maxIndex + 1]) : this.commits.length;
    const limit: number = Number.isFinite(requestedCount) ? Math.max(0, requestedCount) : this.commits.length;
    return this.commits.slice(0, limit).map((commit: GitCommit): string => oneLine
      ? `${commit.id} ${commit.subject}`
      : `commit ${commit.id}\nAuthor: ${commit.author}\nDate:   ${commit.timestamp}\n\n    ${commit.subject}`).join(oneLine ? '\n' : '\n\n');
  }

  private branchCommand(args: string[]): string {
    if (args.length === 0) {
      return this.branches.map((name: string): string => `${name === this.branch ? '* ' : '  '}${name}`).join('\n');
    }
    if (args[0] === '-d' || args[0] === '-D') {
      const name: string = args[1] ?? '';
      if (name === this.branch) {
        return `error: Cannot delete branch '${name}' checked out at '${this.currentPath}'`;
      }
      const index: number = this.branches.indexOf(name);
      if (index < 0) {
        return `error: branch '${name}' not found.`;
      }
      this.branches.splice(index, 1);
      return `Deleted branch ${name}.`;
    }
    const name: string = args[0];
    if (!this.isValidBranchName(name)) {
      return `fatal: '${name}' is not a valid branch name`;
    }
    if (this.branches.includes(name)) {
      return `fatal: a branch named '${name}' already exists`;
    }
    this.branches.push(name);
    return '';
  }

  private switchBranch(args: string[]): string {
    const create: boolean = args[0] === '-c' || args[0] === '-C' || args[0] === '-b';
    const name: string = create ? args[1] ?? '' : args[0] ?? '';
    if (name.length === 0) {
      return 'fatal: missing branch name';
    }
    if (create && !this.branches.includes(name)) {
      if (!this.isValidBranchName(name)) {
        return `fatal: '${name}' is not a valid branch name`;
      }
      this.branches.push(name);
      this.branch = name;
      return `Switched to a new branch '${name}'`;
    }
    if (!this.branches.includes(name)) {
      return `fatal: invalid reference: ${name}`;
    }
    this.branch = name;
    return `Switched to branch '${name}'`;
  }

  private diff(staged: boolean): string {
    const selected: GitFileChange[] = this.files.filter((file: GitFileChange): boolean => file.staged === staged && file.state !== 'untracked');
    if (selected.length === 0) {
      return '';
    }
    return selected.map((file: GitFileChange): string => `diff --git a/${file.path} b/${file.path}\nindex 1a2b3c4..5d6e7f8 100644\n--- a/${file.path}\n+++ b/${file.path}\n@@ -1,2 +1,2 @@\n-// Git Bash source baseline\n+// HarmonyOS PC adaptation`).join('\n\n');
  }

  private remote(args: string[]): string {
    if (args.length === 0 || args[0] === '-v') {
      return this.remotes.join('\n');
    }
    if (args[0] === 'add' && args.length >= 3) {
      const name: string = args[1];
      const url: string = args[2];
      this.remotes.push(`${name}\t${url} (fetch)`);
      this.remotes.push(`${name}\t${url} (push)`);
      return '';
    }
    return `error: unsupported remote command '${args.join(' ')}'`;
  }

  private changeDirectory(args: string[]): string {
    if (args.length > 1) {
      this.lastCommandExitCode = 1;
      return 'bash: cd: too many arguments';
    }
    const destination: string = args[0] ?? '~';
    if (destination === '-' &&
        (this.environment['OLDPWD'] ?? '').length === 0) {
      this.lastCommandExitCode = 1;
      return 'bash: cd: OLDPWD not set';
    }
    const resolvedDestination: string =
      destination === '-' ? this.environment['OLDPWD'] ?? this.homePath : destination;
    if (this.repositoryService !== undefined) {
      const candidate: string = this.resolvePath(resolvedDestination);
      if (!this.repositoryService.directoryExists(candidate)) {
        this.lastCommandExitCode = 1;
        return `bash: cd: ${destination}: No such file or directory`;
      }
      this.setCurrentPath(candidate);
      return destination === '-' ? `${this.currentPath}\n` : '';
    }
    if (destination === '-') {
      this.setCurrentPath(this.normalizePath(resolvedDestination));
      return `${this.currentPath}\n`;
    }
    if (resolvedDestination === '~') {
      this.setCurrentPath(this.homePath);
      return '';
    }
    if (resolvedDestination === '..') {
      const index: number = this.currentPath.lastIndexOf('/');
      const target: string =
        index > 0 ? this.currentPath.slice(0, index) : '/';
      this.setCurrentPath(target);
      return '';
    }
    if (resolvedDestination === '.' || resolvedDestination === 'demo-app') {
      return '';
    }
    if (resolvedDestination === 'workspace') {
      this.setCurrentPath('/home/harmony/workspace');
      return '';
    }
    this.lastCommandExitCode = 1;
    return `bash: cd: ${destination}: No such file or directory`;
  }

  private tokenize(
    input: string,
    includeOperators: boolean = false,
    expandVariables: boolean = true
  ): string[] {
    const tokens: string[] = [];
    let current: string = '';
    let quote: string = '';
    let tokenStarted: boolean = false;
    let escaped: boolean = false;
    for (let index: number = 0; index < input.length; index++) {
      const character: string = input[index];
      if (escaped) {
        current += character;
        tokenStarted = true;
        escaped = false;
      } else if (character === '\\' && quote !== "'") {
        if (quote === '"' &&
            index + 1 < input.length &&
            !['$', '`', '"', '\\', '\n'].includes(input[index + 1])) {
          current += character;
          tokenStarted = true;
        } else {
          escaped = true;
          tokenStarted = true;
        }
      } else if ((character === '"' || character === "'") &&
          (quote.length === 0 || quote === character)) {
        quote = quote.length === 0 ? character : '';
        tokenStarted = true;
      } else if (expandVariables && character === '$' && quote !== "'") {
        const variable = this.readVariable(input, index);
        if (variable.name.length > 0) {
          current += this.variableValue(variable.name);
          tokenStarted = true;
          index = variable.endIndex;
        } else {
          current += character;
          tokenStarted = true;
        }
      } else if (includeOperators && quote.length === 0 &&
          (character === '<' || character === '>')) {
        if (tokenStarted) {
          tokens.push(current);
          current = '';
          tokenStarted = false;
        }
        if (character === '>' && index + 1 < input.length &&
            input[index + 1] === '>') {
          tokens.push('>>');
          index++;
        } else {
          tokens.push(character);
        }
      } else if ((character === ' ' || character === '\t') &&
          quote.length === 0) {
        if (tokenStarted) {
          tokens.push(current);
          current = '';
          tokenStarted = false;
        }
      } else {
        current += character;
        tokenStarted = true;
      }
    }
    if (tokenStarted) {
      tokens.push(current);
    }
    return tokens;
  }

  private splitPipeline(input: string): string[] {
    const stages: string[] = [];
    let current: string = '';
    let quote: string = '';
    let escaped: boolean = false;
    for (let index: number = 0; index < input.length; index++) {
      const character: string = input[index];
      if (escaped) {
        current += character;
        escaped = false;
      } else if (character === '\\' && quote !== "'") {
        current += character;
        escaped = true;
      } else if ((character === '"' || character === "'") &&
          (quote.length === 0 || quote === character)) {
        current += character;
        quote = quote.length === 0 ? character : '';
      } else if (character === '|' && quote.length === 0) {
        stages.push(current.trim());
        current = '';
      } else {
        current += character;
      }
    }
    stages.push(current.trim());
    return stages;
  }

  private echo(args: string[]): string {
    const suppressNewline: boolean =
      args.length > 0 && args[0] === '-n';
    const values: string[] =
      suppressNewline ? args.slice(1) : args;
    return `${values.join(' ')}${suppressNewline ? '' : '\n'}`;
  }

  private cat(args: string[], input: string): string {
    if (args.length === 0) {
      return input;
    }
    const contents: string[] = [];
    for (const path of args) {
      const file = this.readShellFile(path);
      if (!file.success) {
        this.lastCommandExitCode = 1;
        return `cat: ${path}: ${file.error}`;
      }
      contents.push(file.content);
    }
    return contents.join('');
  }

  private environmentText(): string {
    return this.environmentNames().map(
      (name: string): string =>
        `${name}=${this.environment[name] ?? ''}`
    ).join('\n');
  }

  private environmentCommand(args: string[], input: string): string {
    const assignments: ShellAssignment[] = this.leadingAssignments(args);
    const savedValues: Record<string, string> = {};
    const wasSet: Record<string, boolean> = {};
    for (const assignment of assignments) {
      wasSet[assignment.name] =
        this.environment[assignment.name] !== undefined;
      savedValues[assignment.name] =
        this.environment[assignment.name] ?? '';
      this.setEnvironment(assignment.name, assignment.value);
    }
    const commandTokens: string[] = args.slice(assignments.length);
    const output: string = commandTokens.length === 0
      ? this.environmentText()
      : this.executeCommand(commandTokens, input);
    this.restoreAssignments(assignments, savedValues, wasSet);
    return output;
  }

  private printEnvironment(args: string[]): string {
    if (args.length === 0) {
      return this.environmentText();
    }
    const values: string[] = [];
    let missing: boolean = false;
    for (const name of args) {
      if (this.environment[name] === undefined) {
        missing = true;
      } else {
        values.push(this.environment[name] ?? '');
      }
    }
    if (missing) {
      this.lastCommandExitCode = 1;
    }
    return values.join('\n');
  }

  private exportEnvironment(args: string[]): string {
    if (args.length === 0) {
      return this.environmentNames().map(
        (name: string): string =>
          `declare -x ${name}="${(this.environment[name] ?? '').replace(/"/g, '\\"')}"`
      ).join('\n');
    }
    for (const value of args) {
      if (value === '-p') {
        continue;
      }
      const assignment: ShellAssignment | undefined =
        this.parseAssignment(value);
      if (assignment !== undefined) {
        this.setEnvironment(assignment.name, assignment.value);
      } else if (!this.isValidVariableName(value)) {
        this.lastCommandExitCode = 1;
        return `bash: export: '${value}': not a valid identifier`;
      } else if (this.environment[value] === undefined) {
        this.setEnvironment(value, '');
      }
    }
    return '';
  }

  private unsetEnvironmentCommand(args: string[]): string {
    for (const name of args) {
      if (!this.isValidVariableName(name)) {
        this.lastCommandExitCode = 1;
        return `bash: unset: '${name}': not a valid identifier`;
      }
      this.unsetEnvironment(name);
    }
    return '';
  }

  private extractRedirections(tokens: string[]): ShellRedirection {
    const result: ShellRedirection = {
      commandTokens: [],
      inputPath: '',
      outputPath: '',
      append: false,
      error: ''
    };
    for (let index: number = 0; index < tokens.length; index++) {
      const value: string = tokens[index];
      if (value !== '<' && value !== '>' && value !== '>>') {
        result.commandTokens.push(value);
        continue;
      }
      if (index + 1 >= tokens.length ||
          tokens[index + 1] === '<' ||
          tokens[index + 1] === '>' ||
          tokens[index + 1] === '>>') {
        result.error = `syntax error near unexpected token \`${value}'`;
        return result;
      }
      const path: string = tokens[++index];
      if (value === '<') {
        if (result.inputPath.length > 0) {
          result.error = 'ambiguous input redirect';
          return result;
        }
        result.inputPath = path;
      } else {
        if (result.outputPath.length > 0) {
          result.error = 'ambiguous output redirect';
          return result;
        }
        result.outputPath = path;
        result.append = value === '>>';
      }
    }
    return result;
  }

  private leadingAssignments(tokens: string[]): ShellAssignment[] {
    const assignments: ShellAssignment[] = [];
    for (const value of tokens) {
      const assignment: ShellAssignment | undefined =
        this.parseAssignment(value);
      if (assignment === undefined) {
        break;
      }
      assignments.push(assignment);
    }
    return assignments;
  }

  private parseAssignment(value: string): ShellAssignment | undefined {
    const separator: number = value.indexOf('=');
    if (separator <= 0) {
      return undefined;
    }
    const name: string = value.slice(0, separator);
    if (!this.isValidVariableName(name)) {
      return undefined;
    }
    return {
      name,
      value: value.slice(separator + 1)
    };
  }

  private isValidVariableName(name: string): boolean {
    if (name.length === 0) {
      return false;
    }
    const first: number = name.charCodeAt(0);
    if (!this.isLetter(first) && first !== 95) {
      return false;
    }
    for (let index: number = 1; index < name.length; index++) {
      const code: number = name.charCodeAt(index);
      if (!this.isLetter(code) &&
          (code < 48 || code > 57) &&
          code !== 95) {
        return false;
      }
    }
    return true;
  }

  private isLetter(code: number): boolean {
    return (code >= 65 && code <= 90) ||
      (code >= 97 && code <= 122);
  }

  private readVariable(input: string, startIndex: number): ShellVariable {
    if (startIndex + 1 >= input.length) {
      return { name: '', endIndex: startIndex };
    }
    const next: string = input[startIndex + 1];
    if (next === '?') {
      return { name: '?', endIndex: startIndex + 1 };
    }
    if (next === '{') {
      const close: number = input.indexOf('}', startIndex + 2);
      if (close < 0) {
        return { name: '', endIndex: startIndex };
      }
      const name: string = input.slice(startIndex + 2, close);
      return this.isValidVariableName(name) || name === '?'
        ? { name, endIndex: close }
        : { name: '', endIndex: startIndex };
    }
    const firstCode: number = next.charCodeAt(0);
    if (!this.isLetter(firstCode) && firstCode !== 95) {
      return { name: '', endIndex: startIndex };
    }
    let end: number = startIndex + 2;
    while (end < input.length) {
      const code: number = input.charCodeAt(end);
      if (!this.isLetter(code) &&
          (code < 48 || code > 57) &&
          code !== 95) {
        break;
      }
      end++;
    }
    return {
      name: input.slice(startIndex + 1, end),
      endIndex: end - 1
    };
  }

  private expandVariablesInToken(input: string): string {
    let output: string = '';
    for (let index: number = 0; index < input.length; index++) {
      if (input[index] !== '$') {
        output += input[index];
        continue;
      }
      const variable: ShellVariable = this.readVariable(input, index);
      if (variable.name.length === 0) {
        output += '$';
        continue;
      }
      output += this.variableValue(variable.name);
      index = variable.endIndex;
    }
    return output;
  }

  private variableValue(name: string): string {
    return name === '?'
      ? `${this.lastExpansionExitCode}`
      : this.environment[name] ?? '';
  }

  private setEnvironment(name: string, value: string): void {
    this.environment[name] = value;
  }

  private unsetEnvironment(name: string): void {
    this.environment[name] = undefined;
  }

  private environmentNames(): string[] {
    return Object.keys(this.environment).filter(
      (name: string): boolean =>
        this.environment[name] !== undefined
    ).sort();
  }

  private restoreAssignments(
    assignments: ShellAssignment[],
    savedValues: Record<string, string>,
    wasSet: Record<string, boolean>
  ): void {
    for (const assignment of assignments) {
      if (wasSet[assignment.name]) {
        this.setEnvironment(
          assignment.name,
          savedValues[assignment.name]
        );
      } else {
        this.unsetEnvironment(assignment.name);
      }
    }
  }

  private setCurrentPath(path: string): void {
    if (path !== this.currentPath) {
      this.setEnvironment('OLDPWD', this.currentPath);
    }
    this.currentPath = path;
    this.setEnvironment('PWD', path);
  }

  private readShellFile(path: string): ShellFileReadResult {
    const resolvedPath: string = this.resolvePath(path);
    if (this.repositoryService !== undefined) {
      try {
        return {
          success: true,
          content: this.repositoryService.readFile(
            this.currentPath,
            resolvedPath
          ),
          error: ''
        };
      } catch (error) {
        return {
          success: false,
          content: '',
          error: this.errorMessage(error)
        };
      }
    }
    if (this.redirectedFiles[resolvedPath] === undefined) {
      return {
        success: false,
        content: '',
        error: 'No such file or directory'
      };
    }
    return {
      success: true,
      content: this.redirectedFiles[resolvedPath],
      error: ''
    };
  }

  private writeShellFile(
    path: string,
    content: string,
    append: boolean
  ): ShellFileWriteResult {
    const resolvedPath: string = this.resolvePath(path);
    if (this.repositoryService !== undefined) {
      const operation: RepositoryOperation =
        this.repositoryService.writeFile(
          this.currentPath,
          resolvedPath,
          content,
          append
        );
      return operation.success
        ? { success: true, error: '' }
        : { success: false, error: operation.error };
    }
    const existing: string =
      append ? this.redirectedFiles[resolvedPath] ?? '' : '';
    this.redirectedFiles[resolvedPath] = existing + content;
    return { success: true, error: '' };
  }

  private printf(args: string[]): string {
    if (args.length === 0) {
      return 'printf: usage: printf FORMAT [ARGUMENT]...';
    }
    const format: string = args[0];
    let output: string = '';
    let argumentIndex: number = 1;
    let firstPass: boolean = true;
    while (firstPass || argumentIndex < args.length) {
      firstPass = false;
      const passArgumentIndex: number = argumentIndex;
      for (let index: number = 0; index < format.length; index++) {
        const character: string = format[index];
        if (character === '\\' && index + 1 < format.length) {
          const escaped: string = format[++index];
          if (escaped === 'n') {
            output += '\n';
          } else if (escaped === 'r') {
            output += '\r';
          } else if (escaped === 't') {
            output += '\t';
          } else if (escaped === 'b') {
            output += '\b';
          } else if (escaped === 'f') {
            output += '\f';
          } else if (escaped === 'v') {
            output += '\v';
          } else if (escaped.charCodeAt(0) >= 48 &&
              escaped.charCodeAt(0) <= 55) {
            let octal: number = escaped.charCodeAt(0) - 48;
            let digits: number = 1;
            while (digits < 3 && index + 1 < format.length) {
              const nextCode: number = format.charCodeAt(index + 1);
              if (nextCode < 48 || nextCode > 55) {
                break;
              }
              octal = octal * 8 + nextCode - 48;
              index++;
              digits++;
            }
            output += String.fromCharCode(octal);
          } else {
            output += escaped;
          }
        } else if (character === '%' && index + 1 < format.length) {
          const directive: string = format[++index];
          if (directive === '%') {
            output += '%';
          } else if (directive === 's') {
            output += args[argumentIndex++] ?? '';
          } else if (directive === 'b') {
            output += this.expandEscapes(args[argumentIndex++] ?? '');
          } else {
            return `bash: printf: %${directive}: invalid format character`;
          }
        } else {
          output += character;
        }
      }
      if (argumentIndex === passArgumentIndex) {
        break;
      }
    }
    return output;
  }

  private expandEscapes(input: string): string {
    let output: string = '';
    for (let index: number = 0; index < input.length; index++) {
      const character: string = input[index];
      if (character !== '\\' || index + 1 >= input.length) {
        output += character;
        continue;
      }
      const escaped: string = input[++index];
      if (escaped === 'n') {
        output += '\n';
      } else if (escaped === 'r') {
        output += '\r';
      } else if (escaped === 't') {
        output += '\t';
      } else if (escaped === 'b') {
        output += '\b';
      } else if (escaped === 'f') {
        output += '\f';
      } else if (escaped === 'v') {
        output += '\v';
      } else if (escaped.charCodeAt(0) >= 48 &&
          escaped.charCodeAt(0) <= 55) {
        let octal: number = escaped.charCodeAt(0) - 48;
        let digits: number = 1;
        while (digits < 3 && index + 1 < input.length) {
          const nextCode: number = input.charCodeAt(index + 1);
          if (nextCode < 48 || nextCode > 55) {
            break;
          }
          octal = octal * 8 + nextCode - 48;
          index++;
          digits++;
        }
        output += String.fromCharCode(octal);
      } else {
        output += escaped;
      }
    }
    return output;
  }

  private inputLines(input: string): string[] {
    return input.split('\n').map(
      (line: string): string =>
        line.endsWith('\r') ? line.slice(0, line.length - 1) : line
    ).filter((line: string): boolean => line.length > 0);
  }

  private commitId(message: string): string {
    let hash: number = 5381;
    const seed: string = `${message}${this.commits.length}${this.branch}`;
    for (let index: number = 0; index < seed.length; index++) {
      hash = ((hash << 5) + hash) + seed.charCodeAt(index);
      hash = hash & 0x7fffffff;
    }
    return hash.toString(16).padStart(7, '0').slice(-7);
  }

  private isValidBranchName(name: string): boolean {
    return name.length > 0 && !name.includes(' ') && !name.includes('..') && !name.endsWith('/');
  }

  private helpText(): string {
    return 'Harmony Git Bash compatibility session\n\nShell: pwd, ls, cd, echo, printf, cat, env, printenv, export, unset, set, clear, help, basic pipes, <, > and >>\nGit: open, status, rev-parse, add, rm, mv, clean, restore, reset --hard, commit -m, log, show, cat-file, hash-object, ls-tree, ls-files, check-ignore, show-ref, rev-list, merge-base, for-each-ref, symbolic-ref, update-ref, tag, reflog, config, branch, switch, checkout, diff, remote, ls-remote, fetch, clone, pull, push, init\n\nLocal repository reads and writes use the Harmony native Git service. HTTPS ls-remote, fetch, clone, fast-forward pull and push use HarmonyOS NetworkKit with one-shot terminal credential prompts. SSH and secure credential persistence remain in progress.';
  }

  private currentRepository(): RepositorySnapshot | undefined {
    if (this.repositoryService === undefined) {
      return undefined;
    }
    return this.repositoryService.inspect(this.currentPath);
  }

  private nativeStatus(
    snapshot: RepositorySnapshot | undefined,
    short: boolean
  ): string {
    if (snapshot === undefined || !snapshot.valid) {
      const detail: string =
        snapshot === undefined ? this.currentPath : snapshot.error;
      return `fatal: ${detail}`;
    }
    if (short) {
      return snapshot.files.map(
        (file: RepositoryFileStatus): string =>
          file.tracked
            ? `${file.indexState}${file.workTreeState} ${file.path}`
            : `?? ${file.path}`
      ).join('\n');
    }

    let output: string = snapshot.detached
      ? `HEAD detached at ${snapshot.branch}`
      : `On branch ${snapshot.branch}`;
    if (snapshot.head.length === 0) {
      output += `\n\nNo commits yet on ${snapshot.branch}`;
    }
    if (snapshot.files.length === 0) {
      return `${output}\n\nnothing to commit, working tree clean`;
    }

    const staged: RepositoryFileStatus[] = snapshot.files.filter(
      (file: RepositoryFileStatus): boolean => file.staged
    );
    const unstaged: RepositoryFileStatus[] = snapshot.files.filter(
      (file: RepositoryFileStatus): boolean =>
        file.tracked && file.workTreeState !== ' '
    );
    const untracked: RepositoryFileStatus[] = snapshot.files.filter(
      (file: RepositoryFileStatus): boolean => !file.tracked
    );
    if (staged.length > 0) {
      output += '\n\nChanges to be committed:\n';
      for (const file of staged) {
        output += `\t${this.nativeStateLabel(file.indexState)}:   ${file.path}\n`;
      }
    }
    if (unstaged.length > 0) {
      output += '\nChanges not staged for commit:\n';
      for (const file of unstaged) {
        output += `\t${this.nativeStateLabel(file.workTreeState)}:   ${file.path}\n`;
      }
    }
    if (untracked.length > 0) {
      output += '\nUntracked files:\n';
      for (const file of untracked) {
        output += `\t${file.path}\n`;
      }
    }
    return output.trimEnd();
  }

  private nativeBranch(
    snapshot: RepositorySnapshot,
    args: string[]
  ): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    if (args.length === 0) {
      return snapshot.branches.map(
        (name: string): string =>
          `${!snapshot.detached && name === snapshot.branch ? '* ' : '  '}${name}`
      ).join('\n');
    }
    const deleteMode: boolean = args[0] === '-d' || args[0] === '-D';
    if (deleteMode) {
      const name: string = args[1] ?? '';
      const operation: RepositoryOperation =
        this.repositoryService.deleteBranch(
          this.currentPath,
          name,
          args[0] === '-D'
        );
      if (!operation.success) {
        return `error: ${operation.error}`;
      }
      return `Deleted branch ${name}.`;
    }
    if (args[0] === '-m' || args[0] === '-M' ||
        args[0] === '-c' || args[0] === '-C') {
      const names: string[] = args.slice(1).filter(
        (value: string): boolean => !value.startsWith('-')
      );
      if (names.length === 0 || names.length > 2) {
        return 'fatal: branch rename or copy requires a branch name';
      }
      const oldName: string =
        names.length === 1 ? snapshot.branch : names[0];
      const newName: string =
        names.length === 1 ? names[0] : names[1];
      if (oldName === 'HEAD') {
        return 'fatal: cannot rename symbolic branch HEAD';
      }
      const force: boolean = args[0] === '-M' || args[0] === '-C';
      const operation: RepositoryOperation =
        args[0] === '-m' || args[0] === '-M'
          ? this.repositoryService.moveBranch(
              this.currentPath,
              oldName,
              newName,
              force
            )
          : this.repositoryService.copyBranch(
              this.currentPath,
              oldName,
              newName,
              force
            );
      if (!operation.success) {
        return `fatal: ${operation.error}`;
      }
      return '';
    }
    const name: string = args[0];
    const operation: RepositoryOperation =
      this.repositoryService.createBranch(this.currentPath, name, false);
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    return '';
  }

  private nativeStage(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const paths: string[] = args.filter(
      (value: string): boolean => value !== '--'
    );
    const operation: RepositoryOperation =
      this.repositoryService.stage(this.currentPath, paths);
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    if (operation.changedCount === 0) {
      return '';
    }
    return `Staged ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}.`;
  }

  private nativeRemove(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let cached: boolean = false;
    let force: boolean = false;
    let recursive: boolean = false;
    let parseOptions: boolean = true;
    const paths: string[] = [];
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--cached') {
        cached = true;
      } else if (parseOptions && (value === '-f' || value === '--force')) {
        force = true;
      } else if (parseOptions &&
          (value === '-r' || value === '-R' || value === '--recursive')) {
        recursive = true;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        paths.push(value);
      }
    }
    if (paths.length === 0) {
      return 'fatal: No pathspec was given. Which files should I remove?';
    }
    const operation: RepositoryOperation =
      this.repositoryService.remove(
        this.currentPath,
        paths,
        cached,
        force,
        recursive
      );
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    const suffix: string = cached ? ' from the index' : '';
    return `Removed ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}${suffix}.`;
  }

  private nativeClean(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryCleanOptions = {
      dryRun: false,
      directories: false,
      quiet: false,
      removeIgnored: false,
      ignoredOnly: false,
      force: 0,
      excludes: [],
      paths: []
    };
    let parseOptions: boolean = true;
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
        continue;
      }
      if (parseOptions && value === '--dry-run') {
        options.dryRun = true;
        continue;
      }
      if (parseOptions && value === '--directories') {
        options.directories = true;
        continue;
      }
      if (parseOptions && (value === '-q' || value === '--quiet')) {
        options.quiet = true;
        continue;
      }
      if (parseOptions && (value === '-x' || value === '--force-remove-ignored')) {
        if (options.ignoredOnly) {
          return 'fatal: cannot combine -x and -X';
        }
        options.removeIgnored = true;
        continue;
      }
      if (parseOptions && (value === '-X' || value === '--only-ignored')) {
        if (options.removeIgnored) {
          return 'fatal: cannot combine -x and -X';
        }
        options.ignoredOnly = true;
        continue;
      }
      if (parseOptions && value === '-i') {
        return 'fatal: git clean -i requires the PTY-backed shell input stream';
      }
      if (parseOptions && value === '--interactive') {
        return 'fatal: git clean -i requires the PTY-backed shell input stream';
      }
      if (parseOptions && (value === '-e' || value === '--exclude')) {
        if (index + 1 >= args.length) {
          return `error: option '${value}' requires a value`;
        }
        options.excludes.push(args[++index]);
        continue;
      }
      if (parseOptions && value.startsWith('--exclude=')) {
        options.excludes.push(value.slice('--exclude='.length));
        continue;
      }
      if (parseOptions && (value === '-f' || value === '--force')) {
        options.force++;
        continue;
      }
      if (parseOptions && value === '-n') {
        options.dryRun = true;
        continue;
      }
      if (parseOptions && value === '-d') {
        options.directories = true;
        continue;
      }
      if (parseOptions && value.startsWith('-') && value.length > 1) {
        for (const flag of value.slice(1)) {
          if (flag === 'n') {
            options.dryRun = true;
          } else if (flag === 'd') {
            options.directories = true;
          } else if (flag === 'q') {
            options.quiet = true;
          } else if (flag === 'x') {
            if (options.ignoredOnly) {
              return 'fatal: cannot combine -x and -X';
            }
            options.removeIgnored = true;
          } else if (flag === 'X') {
            if (options.removeIgnored) {
              return 'fatal: cannot combine -x and -X';
            }
            options.ignoredOnly = true;
          } else if (flag === 'f') {
            options.force++;
          } else if (flag === 'i') {
            return 'fatal: git clean -i requires the PTY-backed shell input stream';
          } else {
            return `error: unknown switch \`${flag}\``;
          }
        }
        continue;
      }
      options.paths.push(value);
    }

    try {
      const result: RepositoryCleanResult =
        this.repositoryService.clean(this.currentPath, options);
      if (!result.success) {
        return `fatal: ${result.error}`;
      }
      if (options.quiet) {
        return '';
      }
      const lines: string[] = [];
      for (const path of result.cleanedPaths) {
        lines.push(`${options.dryRun ? 'Would remove' : 'Removing'} ${path}`);
      }
      for (const path of result.skippedRepositories) {
        lines.push(
          `${options.dryRun ? 'Would skip repository' : 'Skipping repository'} ${path}`
        );
      }
      return lines.join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeMove(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let force: boolean = false;
    let parseOptions: boolean = true;
    const paths: string[] = [];
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && (value === '-f' || value === '--force')) {
        force = true;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        paths.push(value);
      }
    }
    if (paths.length !== 2) {
      return 'usage: git mv [-f] [--] <source> <destination>';
    }
    const operation: RepositoryOperation =
      this.repositoryService.movePath(
        this.currentPath,
        paths[0],
        paths[1],
        force
      );
    return operation.success ? '' : `fatal: ${operation.error}`;
  }

  private nativeRestore(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let staged: boolean = false;
    let explicitWorktree: boolean = false;
    let source: string = '';
    let sourceRequested: boolean = false;
    let parseOptions: boolean = true;
    const paths: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--staged') {
        staged = true;
      } else if (parseOptions && value === '--worktree') {
        explicitWorktree = true;
      } else if (parseOptions && value === '--source') {
        sourceRequested = true;
        source = args[index + 1] ?? '';
        index++;
      } else if (parseOptions && value.startsWith('--source=')) {
        sourceRequested = true;
        source = value.slice('--source='.length);
      } else {
        paths.push(value);
      }
    }
    if (sourceRequested && source.length === 0) {
      return 'error: option `source` requires a value';
    }
    if (paths.length === 0) {
      return 'git restore: missing pathspec.';
    }
    const worktree: boolean = explicitWorktree || !staged;
    if (sourceRequested || (staged && explicitWorktree)) {
      const operation: RepositoryOperation =
        this.repositoryService.restoreFromSource(
          this.currentPath,
          sourceRequested ? source : 'HEAD',
          paths,
          staged,
          worktree
        );
      if (!operation.success) {
        return `fatal: ${operation.error}`;
      }
      if (operation.changedCount === 0) {
        return '';
      }
      if (staged && worktree) {
        return `Restored ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'} to the index and working tree.`;
      }
      if (staged) {
        return `Restored ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'} to the index.`;
      }
      return `Restored ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}.`;
    }
    if (!staged) {
      const operation: RepositoryOperation =
        this.repositoryService.restoreWorkingTree(this.currentPath, paths);
      if (!operation.success) {
        return `fatal: ${operation.error}`;
      }
      return operation.changedCount === 0
        ? ''
        : `Restored ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}.`;
    }
    const operation: RepositoryOperation =
      this.repositoryService.restoreStaged(this.currentPath, paths);
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    if (operation.changedCount === 0) {
      return '';
    }
    return `Unstaged ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}.`;
  }

  private nativeReset(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    if (args.includes('--hard')) {
      const operation: RepositoryOperation =
        this.repositoryService.resetHard(this.currentPath);
      return operation.success
        ? `HEAD is now at ${operation.snapshot.head.slice(0, 7)}`
        : `fatal: ${operation.error}`;
    }
    const paths: string[] = args.filter(
      (value: string): boolean =>
        value !== '--' && !value.startsWith('-')
    );
    const operation: RepositoryOperation =
      this.repositoryService.restoreStaged(
        this.currentPath,
        paths.length === 0 ? ['.'] : paths
      );
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    return operation.changedCount === 0
      ? ''
      : `Unstaged ${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'}.`;
  }

  private nativeCommit(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const messageIndex: number = args.findIndex(
      (value: string): boolean => value === '-m' || value === '--message'
    );
    if (messageIndex < 0 || messageIndex + 1 >= args.length) {
      return 'error: switch `m` requires a value';
    }
    const message: string = args[messageIndex + 1];
    const operation: RepositoryOperation =
      this.repositoryService.commit(this.currentPath, message);
    if (!operation.success) {
      return operation.error.startsWith('Nothing to commit')
        ? `On branch ${operation.snapshot.branch}\n${operation.error}`
        : `fatal: ${operation.error}`;
    }
    const commitId: string = operation.snapshot.head.slice(0, 7);
    return `[${operation.snapshot.branch} ${commitId}] ${message}\n ` +
      `${operation.changedCount} file${operation.changedCount === 1 ? '' : 's'} changed`;
  }

  private nativeSwitch(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    if (args[0] === '--') {
      return this.nativeRestore(args);
    }
    const create: boolean =
      args[0] === '-c' || args[0] === '-b' ||
      args[0] === '-C' || args[0] === '-B';
    const resetBranch: boolean =
      args[0] === '-C' || args[0] === '-B';
    const name: string = create ? args[1] ?? '' : args[0] ?? '';
    if (name.length === 0) {
      return `fatal: missing branch name`;
    }
    if (create) {
      if (resetBranch) {
        const operation: RepositoryOperation =
          this.repositoryService.checkoutBranch(
            this.currentPath,
            name,
            args[2] ?? ''
          );
        if (!operation.success) {
          return `fatal: ${operation.error}`;
        }
        return `Switched to and reset branch '${name}'`;
      }
      const operation: RepositoryOperation =
        this.repositoryService.createBranch(this.currentPath, name, true);
      if (!operation.success) {
        return `fatal: ${operation.error}`;
      }
      return `Switched to a new branch '${name}'`;
    }
    const operation: RepositoryOperation =
      this.repositoryService.switchBranch(this.currentPath, name);
    if (!operation.success) {
      return `fatal: ${operation.error}`;
    }
    return `Switched to branch '${name}'`;
  }

  private nativeDiff(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const staged: boolean =
      args.includes('--staged') || args.includes('--cached');
    try {
      return this.repositoryService.diff(this.currentPath, staged);
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private parseReflogOptions(
    args: string[],
    defaultPretty: string,
    defaultAbbrevCommit: boolean
  ): ReflogCommandOptions | undefined {
    const options: ReflogCommandOptions = {
      maxCount: 100,
      skip: 0,
      since: '',
      until: '',
      ref: '',
      format: '',
      pretty: defaultPretty,
      date: '',
      abbrevCommit: defaultAbbrevCommit,
      grepReflog: [],
      grep: [],
      authors: [],
      committers: [],
      allMatch: false,
      invertGrep: false,
      ignoreCase: false,
      fixedStrings: false,
      extendedRegexp: false,
      all: false,
      singleWorktree: false
    };
    let parseOptions: boolean = true;
    const readOptionValue = (index: number): string | undefined => {
      return index + 1 < args.length ? args[index + 1] : undefined;
    };
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
        continue;
      }
      if (parseOptions &&
          (value === '-g' || value === '--walk-reflogs')) {
        continue;
      }
      if (parseOptions && value === '--oneline') {
        options.pretty = 'oneline';
        options.abbrevCommit = true;
      } else if (parseOptions && value === '--abbrev-commit') {
        options.abbrevCommit = true;
      } else if (parseOptions && value === '--no-abbrev-commit') {
        options.abbrevCommit = false;
      } else if (parseOptions && value.startsWith('--abbrev=')) {
        const abbreviation: number =
          Number(value.slice('--abbrev='.length));
        if (!Number.isInteger(abbreviation) ||
            abbreviation < 1 ||
            abbreviation > 40) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.abbrevCommit = true;
      } else if (parseOptions &&
          (value === '-n' || value === '--max-count')) {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.maxCount = Number(optionValue);
        index++;
      } else if (parseOptions && value.startsWith('--max-count=')) {
        options.maxCount = Number(value.slice('--max-count='.length));
      } else if (parseOptions && value === '--skip') {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.skip = Number(optionValue);
        index++;
      } else if (parseOptions && value.startsWith('--skip=')) {
        options.skip = Number(value.slice('--skip='.length));
      } else if (parseOptions &&
          (value === '--since' || value === '--after')) {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.since = optionValue;
        index++;
      } else if (parseOptions &&
          (value.startsWith('--since=') || value.startsWith('--after='))) {
        options.since = value.slice(value.indexOf('=') + 1);
      } else if (parseOptions &&
          (value === '--until' || value === '--before')) {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.until = optionValue;
        index++;
      } else if (parseOptions &&
          (value.startsWith('--until=') || value.startsWith('--before='))) {
        options.until = value.slice(value.indexOf('=') + 1);
      } else if (parseOptions &&
          (value === '--format' || value === '--pretty')) {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        if (value === '--format') {
          options.format = optionValue;
        } else {
          options.pretty = optionValue;
        }
        index++;
      } else if (parseOptions &&
          (value === '--grep-reflog' || value === '--grep' ||
            value === '--author' || value === '--committer')) {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        if (value === '--grep-reflog') {
          options.grepReflog.push(optionValue);
        } else if (value === '--grep') {
          options.grep.push(optionValue);
        } else if (value === '--author') {
          options.authors.push(optionValue);
        } else {
          options.committers.push(optionValue);
        }
        index++;
      } else if (parseOptions &&
          (value.startsWith('--grep-reflog=') ||
            value.startsWith('--grep=') ||
            value.startsWith('--author=') ||
            value.startsWith('--committer='))) {
        const separator: number = value.indexOf('=');
        const optionValue: string = value.slice(separator + 1);
        if (value.startsWith('--grep-reflog=')) {
          options.grepReflog.push(optionValue);
        } else if (value.startsWith('--grep=')) {
          options.grep.push(optionValue);
        } else if (value.startsWith('--author=')) {
          options.authors.push(optionValue);
        } else {
          options.committers.push(optionValue);
        }
      } else if (parseOptions &&
          (value === '--all-match' || value === '--invert-grep' ||
            value === '-i' || value === '--regexp-ignore-case' ||
            value === '-F' || value === '--fixed-strings' ||
            value === '-E' || value === '--extended-regexp' ||
            value === '-G' || value === '--basic-regexp' ||
            value === '--all' || value === '--single-worktree')) {
        if (value === '--all-match') {
          options.allMatch = true;
        } else if (value === '--invert-grep') {
          options.invertGrep = true;
        } else if (value === '-i' || value === '--regexp-ignore-case') {
          options.ignoreCase = true;
        } else if (value === '-F' || value === '--fixed-strings') {
          options.fixedStrings = true;
          options.extendedRegexp = false;
        } else if (value === '-E' || value === '--extended-regexp') {
          options.extendedRegexp = true;
          options.fixedStrings = false;
        } else if (value === '-G' || value === '--basic-regexp') {
          options.extendedRegexp = false;
          options.fixedStrings = false;
        } else if (value === '--all') {
          options.all = true;
        } else {
          options.singleWorktree = true;
        }
      } else if (parseOptions &&
          (value.startsWith('--format=') || value.startsWith('--pretty='))) {
        const optionValue: string = value.slice(value.indexOf('=') + 1);
        if (value.startsWith('--format=')) {
          options.format = optionValue;
        } else {
          options.pretty = optionValue;
        }
      } else if (parseOptions && value === '--date') {
        const optionValue: string | undefined = readOptionValue(index);
        if (optionValue === undefined) {
          this.lastCommandExitCode = 129;
          return undefined;
        }
        options.date = optionValue;
        index++;
      } else if (parseOptions && value.startsWith('--date=')) {
        options.date = value.slice('--date='.length);
      } else if (parseOptions && value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return undefined;
      } else if (options.ref.length === 0) {
        options.ref = value;
      } else {
        this.lastCommandExitCode = 129;
        return undefined;
      }
    }
    if (!Number.isInteger(options.maxCount) ||
        options.maxCount < 0 ||
        !Number.isInteger(options.skip) ||
        options.skip < 0 ||
        (options.all && options.ref.length > 0) ||
        (options.singleWorktree && !options.all)) {
      this.lastCommandExitCode = 129;
      return undefined;
    }
    return options;
  }

  private basicReflogPattern(pattern: string): string {
    let result: string = '';
    const operators: string = '+?|(){}';
    for (let index: number = 0; index < pattern.length; index++) {
      const value: string = pattern[index];
      if (value === '\\' && index + 1 < pattern.length) {
        const next: string = pattern[index + 1];
        result += operators.includes(next) ? next : `\\${next}`;
        index++;
      } else {
        result += operators.includes(value) ? `\\${value}` : value;
      }
    }
    return result;
  }

  private matchesReflogPattern(
    value: string,
    pattern: string,
    options: ReflogCommandOptions
  ): boolean {
    if (options.fixedStrings) {
      const actual: string = options.ignoreCase ? value.toLowerCase() : value;
      const expected: string =
        options.ignoreCase ? pattern.toLowerCase() : pattern;
      return actual.includes(expected);
    }
    const source: string = options.extendedRegexp
      ? pattern
      : this.basicReflogPattern(pattern);
    return new RegExp(
      source,
      options.ignoreCase ? 'i' : ''
    ).test(value);
  }

  private matchesReflogGroup(
    value: string,
    patterns: string[],
    options: ReflogCommandOptions,
    allMatch: boolean
  ): boolean {
    if (patterns.length === 0) {
      return true;
    }
    const matches: boolean[] = patterns.map(
      (pattern: string): boolean =>
        this.matchesReflogPattern(value, pattern, options)
    );
    return allMatch
      ? matches.every((matched: boolean): boolean => matched)
      : matches.some((matched: boolean): boolean => matched);
  }

  private reflogEntryMatches(
    entry: RepositoryReflogEntry,
    options: ReflogCommandOptions
  ): boolean {
    const groups: boolean[] = [];
    const reflogText: string = entry.message;
    const commitText: string =
      `${entry.subject}\n${entry.message}`;
    const author: string =
      entry.author.length > 0 ? entry.author : entry.actor;
    const committer: string =
      entry.committer.length > 0 ? entry.committer : entry.actor;
    if (options.grepReflog.length > 0) {
      groups.push(this.matchesReflogGroup(
        reflogText,
        options.grepReflog,
        options,
        false
      ));
    }
    if (options.grep.length > 0) {
      const messageMatch: boolean = this.matchesReflogGroup(
        commitText,
        options.grep,
        options,
        options.allMatch
      );
      groups.push(options.invertGrep ? !messageMatch : messageMatch);
    }
    if (options.authors.length > 0) {
      groups.push(this.matchesReflogGroup(
        author,
        options.authors,
        options,
        false
      ));
    }
    if (options.committers.length > 0) {
      groups.push(this.matchesReflogGroup(
        committer,
        options.committers,
        options,
        false
      ));
    }
    if (groups.length === 0) {
      return true;
    }
    return groups.every((group: boolean): boolean => group);
  }

  private padReflogNumber(value: number, length: number): string {
    return String(value).padStart(length, '0');
  }

  private reflogTimestampSeconds(value: string): number {
    const separator: number = value.indexOf(' ');
    const seconds: number = Number(
      separator < 0 ? value : value.slice(0, separator));
    return Number.isFinite(seconds) ? seconds : 0;
  }

  private hasReflogContentFilters(
    options: ReflogCommandOptions
  ): boolean {
    return options.grepReflog.length > 0 ||
      options.grep.length > 0 ||
      options.authors.length > 0 ||
      options.committers.length > 0;
  }

  private reflogEntryRef(entry: RepositoryReflogEntry): string {
    const selectorStart: number = entry.selector.lastIndexOf('@{');
    return selectorStart > 0
      ? entry.selector.slice(0, selectorStart)
      : 'HEAD';
  }

  private orderAllReflogEntries(
    entries: RepositoryReflogEntry[]
  ): RepositoryReflogEntry[] {
    return entries.slice().sort((
      left: RepositoryReflogEntry,
      right: RepositoryReflogEntry
    ): number => {
      const timestampDifference: number =
        this.reflogTimestampSeconds(right.timestamp) -
          this.reflogTimestampSeconds(left.timestamp);
      if (timestampDifference !== 0) {
        return timestampDifference;
      }
      const leftRef: string = this.reflogEntryRef(left);
      const rightRef: string = this.reflogEntryRef(right);
      if (leftRef === 'HEAD' && rightRef !== 'HEAD') {
        return 1;
      }
      if (rightRef === 'HEAD' && leftRef !== 'HEAD') {
        return -1;
      }
      if (leftRef !== rightRef) {
        return leftRef < rightRef ? -1 : 1;
      }
      return left.index - right.index;
    });
  }

  private limitReflogEntries(
    entries: RepositoryReflogEntry[],
    options: ReflogCommandOptions
  ): RepositoryReflogEntry[] {
    const start: number = Math.min(options.skip, entries.length);
    const end: number = Math.min(
      start + options.maxCount,
      entries.length);
    return entries.slice(start, end);
  }

  private formatReflogDate(value: string, mode: string): string {
    const seconds: number = this.reflogTimestampSeconds(value);
    if (value.length === 0 || seconds === 0) {
      return value;
    }
    const separator: number = value.indexOf(' ');
    const timezone: string =
      separator < 0 ? '+0000' : value.slice(separator + 1);
    let offsetSeconds: number = 0;
    if (timezone.length === 5 &&
        (timezone[0] === '+' || timezone[0] === '-') &&
        /^\d{4}$/.test(timezone.slice(1))) {
      offsetSeconds =
        (Number(timezone.slice(1, 3)) * 60 +
          Number(timezone.slice(3, 5))) * 60;
      if (timezone[0] === '-') {
        offsetSeconds = -offsetSeconds;
      }
    }
    const date: Date = new Date((seconds + offsetSeconds) * 1000);
    const year: number = date.getUTCFullYear();
    const month: string = this.padReflogNumber(
      date.getUTCMonth() + 1,
      2);
    const day: string = this.padReflogNumber(date.getUTCDate(), 2);
    const clock: string =
      `${this.padReflogNumber(date.getUTCHours(), 2)}:` +
      `${this.padReflogNumber(date.getUTCMinutes(), 2)}:` +
      `${this.padReflogNumber(date.getUTCSeconds(), 2)}`;
    const weekdays: string[] =
      ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    const months: string[] =
      ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
        'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    const defaultDate: string =
      `${weekdays[date.getUTCDay()]} ${months[date.getUTCMonth()]} ` +
      `${date.getUTCDate()} ${clock} ${year} ${timezone}`;
    if (mode === 'unix') {
      return String(seconds);
    }
    if (mode === 'raw') {
      return `${seconds} ${timezone}`;
    }
    if (mode === 'short') {
      return `${year}-${month}-${day}`;
    }
    if (mode === 'iso' || mode === 'iso8601') {
      return `${year}-${month}-${day} ${clock} ${timezone}`;
    }
    if (mode === 'iso-strict') {
      return `${year}-${month}-${day}T${clock}` +
        `${timezone.slice(0, 3)}:${timezone.slice(3)}`;
    }
    if (mode === 'rfc' || mode === 'rfc2822') {
      return `${weekdays[date.getUTCDay()]}, ${day} ` +
        `${months[date.getUTCMonth()]} ${year} ${clock} ${timezone}`;
    }
    return defaultDate;
  }

  private reflogIdentityPart(actor: string, email: boolean): string {
    const open: number = actor.lastIndexOf('<');
    const close: number = actor.lastIndexOf('>');
    if (open < 0 || close <= open) {
      return email ? '' : actor;
    }
    return email
      ? actor.slice(open + 1, close)
      : actor.slice(0, open).trim();
  }

  private shortenedReflogRef(ref: string): string {
    const prefixes: string[] = [
      'refs/heads/',
      'refs/remotes/',
      'refs/tags/'
    ];
    for (const prefix of prefixes) {
      if (ref.startsWith(prefix)) {
        return ref.slice(prefix.length);
      }
    }
    return ref;
  }

  private reflogSelector(
    entry: RepositoryReflogEntry,
    ref: string,
    date: string
  ): string {
    if (date.length === 0 && entry.selector.length > 0) {
      return entry.selector;
    }
    const selectorStart: number = ref.lastIndexOf('@{');
    const baseRef: string =
      selectorStart >= 0 ? ref.slice(0, selectorStart) : ref;
    const displayRef: string = baseRef.length === 0 ? 'HEAD' : baseRef;
    const selectorValue: string = date.length > 0
      ? this.formatReflogDate(entry.timestamp, date)
      : String(entry.index);
    return `${displayRef}@{${selectorValue}}`;
  }

  private reflogFormatValue(
    token: string,
    entry: RepositoryReflogEntry,
    ref: string,
    options: ReflogCommandOptions
  ): string {
    const selector: string =
      this.reflogSelector(entry, ref, options.date);
    const shortSelector: string =
      selector.slice(0, selector.lastIndexOf('@{'));
    const commitTimestamp: string =
      entry.commitTimestamp.length > 0
        ? entry.commitTimestamp
        : entry.timestamp;
    const authorTimestamp: string =
      entry.authorTimestamp.length > 0
        ? entry.authorTimestamp
        : commitTimestamp;
    const subject: string =
      entry.subject.length > 0 ? entry.subject : entry.message;
    const author: string =
      entry.author.length > 0 ? entry.author : entry.actor;
    const committer: string =
      entry.committer.length > 0 ? entry.committer : entry.actor;
    if (token === 'H') {
      return entry.newId;
    }
    if (token === 'h') {
      return entry.newId.slice(0, options.abbrevCommit ? 7 : 40);
    }
    if (token === 'gD') {
      return selector;
    }
    if (token === 'gd') {
      return `${this.shortenedReflogRef(shortSelector)}@{` +
        selector.slice(selector.lastIndexOf('@{') + 2);
    }
    if (token === 'gs') {
      return entry.message;
    }
    if (token === 'gn' || token === 'gN') {
      return this.reflogIdentityPart(entry.actor, false);
    }
    if (token === 'ge' || token === 'gE') {
      return this.reflogIdentityPart(entry.actor, true);
    }
    if (token === 's') {
      return subject;
    }
    if (token === 'an' || token === 'aN') {
      return this.reflogIdentityPart(author, false);
    }
    if (token === 'cn' || token === 'cN') {
      return this.reflogIdentityPart(committer, false);
    }
    if (token === 'ae' || token === 'aE') {
      return this.reflogIdentityPart(author, true);
    }
    if (token === 'ce' || token === 'cE') {
      return this.reflogIdentityPart(committer, true);
    }
    if (token === 'at') {
      return String(this.reflogTimestampSeconds(authorTimestamp));
    }
    if (token === 'ct') {
      return String(this.reflogTimestampSeconds(commitTimestamp));
    }
    if (token === 'ad') {
      return this.formatReflogDate(
        authorTimestamp,
        options.date.length === 0 ? 'default' : options.date);
    }
    if (token === 'cd') {
      return this.formatReflogDate(
        commitTimestamp,
        options.date.length === 0 ? 'default' : options.date);
    }
    if (token === 'ai') {
      return this.formatReflogDate(authorTimestamp, 'iso');
    }
    if (token === 'ci') {
      return this.formatReflogDate(commitTimestamp, 'iso');
    }
    if (token === 'aI') {
      return this.formatReflogDate(authorTimestamp, 'iso-strict');
    }
    if (token === 'cI') {
      return this.formatReflogDate(commitTimestamp, 'iso-strict');
    }
    if (token === '%') {
      return '%';
    }
    return `%${token}`;
  }

  private formatReflogTemplate(
    template: string,
    entry: RepositoryReflogEntry,
    ref: string,
    options: ReflogCommandOptions
  ): string {
    let output: string = '';
    for (let index: number = 0; index < template.length; index++) {
      if (template[index] !== '%') {
        output += template[index];
        continue;
      }
      if (index + 1 >= template.length) {
        output += '%';
        continue;
      }
      const next: string = template[index + 1];
      if (next === 'n') {
        output += '\n';
        index++;
        continue;
      }
      if (next === 'x' && index + 3 < template.length) {
        const code: string = template.slice(index + 2, index + 4);
        const parsed: number = Number.parseInt(code, 16);
        if (Number.isFinite(parsed)) {
          output += String.fromCharCode(parsed);
          index += 3;
          continue;
        }
      }
      const tokenLength: number =
        next === 'g' || next === 'a' || next === 'c'
          ? 2
          : 1;
      const token: string = template.slice(
        index + 1,
        index + 1 + tokenLength);
      output += this.reflogFormatValue(
        token,
        entry,
        ref,
        options);
      index += tokenLength;
    }
    return output;
  }

  private formatReflogEntry(
    entry: RepositoryReflogEntry,
    options: ReflogCommandOptions,
    walkReflogs: boolean
  ): string {
    const ref: string = options.all
      ? this.reflogEntryRef(entry)
      : options.ref.length === 0 ? 'HEAD' : options.ref;
    const selector: string =
      this.reflogSelector(entry, ref, options.date);
    const subject: string =
      entry.subject.length > 0 ? entry.subject : entry.message;
    const pretty: string =
      options.format.length > 0 ? `format:${options.format}` : options.pretty;
    if (pretty === 'oneline') {
      const id: string = options.abbrevCommit
        ? entry.newId.slice(0, 7)
        : entry.newId;
      return `${id} ${selector}: ${entry.message}`;
    }
    if (pretty === 'reference') {
      return `${entry.newId} (${selector})`;
    }
    if (pretty.startsWith('format:') ||
        pretty.startsWith('tformat:')) {
      return this.formatReflogTemplate(
        pretty.slice(pretty.indexOf(':') + 1),
        entry,
        ref,
        options);
    }
    if (!walkReflogs && options.format.length === 0) {
      const id: string = entry.newId.slice(0, 7);
      return `${id} ${selector}: ${entry.message}`;
    }
    const commitTimestamp: string =
      entry.commitTimestamp.length > 0
        ? entry.commitTimestamp
        : entry.timestamp;
    return `commit ${entry.newId}\n` +
      `Reflog: ${selector} (${entry.actor})\n` +
      `Reflog message: ${entry.message}\n` +
      `Author: ${entry.author.length > 0 ? entry.author : entry.actor}\n` +
      `Date:   ${this.formatReflogDate(
        commitTimestamp,
        options.date.length === 0 ? 'default' : options.date)}\n\n` +
      `    ${subject}`;
  }

  private nativeReflogShow(
    args: string[],
    walkReflogs: boolean
  ): string {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: ReflogCommandOptions | undefined =
      this.parseReflogOptions(
        args,
        walkReflogs ? 'medium' : 'oneline',
        !walkReflogs);
    if (options === undefined) {
      return walkReflogs
        ? 'usage: git log -g [<log-options>] [<ref>]'
        : 'usage: git reflog [show] [<log-options>] [<ref>]';
    }
    try {
      const separator: string =
        options.pretty === 'oneline' ||
        options.format.length > 0 ||
        !walkReflogs
          ? '\n'
          : '\n\n';
      const refs: string[] = options.all
        ? this.repositoryService.listReflogs(this.currentPath)
        : [options.ref];
      const requiresShellLimit: boolean =
        options.all || this.hasReflogContentFilters(options);
      const fetchMaxCount: number =
        requiresShellLimit ? 4294967295 : options.maxCount;
      const fetchSkip: number =
        requiresShellLimit ? 0 : options.skip;
      let entries: RepositoryReflogEntry[] = [];
      for (const ref of refs) {
        const refEntries: RepositoryReflogEntry[] =
          this.repositoryService.reflog(
            this.currentPath,
            ref,
            fetchMaxCount,
            fetchSkip,
            options.since,
            options.until);
        entries.push(...refEntries);
      }
      if (options.all) {
        entries = this.orderAllReflogEntries(entries);
      }
      entries = entries.filter(
          (entry: RepositoryReflogEntry): boolean =>
            this.reflogEntryMatches(entry, options)
        );
      if (requiresShellLimit) {
        entries = this.limitReflogEntries(entries, options);
      }
      const output: string[] = entries.map(
          (entry: RepositoryReflogEntry): string =>
            this.formatReflogEntry(entry, options, walkReflogs)
        );
      this.lastCommandExitCode = 0;
      return output.join(separator);
    } catch (error) {
      this.lastCommandExitCode = 128;
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeLog(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    if (args.includes('-g') || args.includes('--walk-reflogs')) {
      return this.nativeReflogShow(args, true);
    }
    const oneLine: boolean = args.includes('--oneline');
    const maxIndex: number = args.findIndex(
      (value: string): boolean =>
        value === '-n' || value === '--max-count'
    );
    const requestedCount: number =
      maxIndex >= 0 ? Number(args[maxIndex + 1]) : 100;
    const limit: number =
      Number.isFinite(requestedCount) ? Math.max(0, requestedCount) : 100;
    try {
      return this.repositoryService.log(this.currentPath, limit).map(
        (commit: GitCommit): string => oneLine
          ? `${commit.id.slice(0, 7)} ${commit.subject}`
          : `commit ${commit.id}\nAuthor: ${commit.author}\nDate:   ${commit.timestamp}\n\n    ${commit.subject}`
      ).join(oneLine ? '\n' : '\n\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeShow(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let statOnly: boolean = false;
    let oneLine: boolean = false;
    let revision: string = '';
    let pathMode: boolean = false;
    const paths: string[] = [];
    for (const value of args) {
      if (pathMode) {
        paths.push(value);
      } else if (value === '--stat') {
        statOnly = true;
      } else if (value === '--oneline') {
        oneLine = true;
      } else if (value === '--') {
        pathMode = true;
      } else if (value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else if (revision.length === 0) {
        revision = value;
      } else {
        return 'usage: git show [--stat] [--oneline] [<revision>]';
      }
    }
    try {
      return this.repositoryService.show(
        this.currentPath,
        revision,
        statOnly,
        oneLine,
        paths
      );
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeListFiles(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryListFilesOptions = {
      cached: false,
      modified: false,
      deleted: false,
      others: false,
      ignored: false,
      excludeStandard: false,
      stage: false,
      fullName: false,
      paths: []
    };
    let parseOptions: boolean = true;
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '-c' || value === '--cached')) {
        options.cached = true;
      } else if (parseOptions &&
          (value === '-m' || value === '--modified')) {
        options.modified = true;
      } else if (parseOptions &&
          (value === '-d' || value === '--deleted')) {
        options.deleted = true;
      } else if (parseOptions &&
          (value === '-o' || value === '--others')) {
        options.others = true;
      } else if (parseOptions &&
          (value === '-i' || value === '--ignored')) {
        options.ignored = true;
      } else if (parseOptions && value === '--exclude-standard') {
        options.excludeStandard = true;
      } else if (parseOptions &&
          (value === '-s' || value === '--stage')) {
        options.stage = true;
      } else if (parseOptions && value === '--full-name') {
        options.fullName = true;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        options.paths.push(value);
      }
    }
    if (options.ignored && !options.others) {
      return 'fatal: git ls-files --ignored requires --others';
    }
    try {
      return this.repositoryService.listFiles(
        this.currentPath,
        options
      ).join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeHashObject(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let type: string = 'blob';
    let write: boolean = false;
    let stdinMode: boolean = false;
    let stdinPathsMode: boolean = false;
    let parseOptions: boolean = true;
    const paths: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '-w') {
        write = true;
      } else if (parseOptions &&
          (value === '-t' || value === '--type')) {
        if (index + 1 >= args.length) {
          return `error: option '${value}' requires a value`;
        }
        type = args[++index];
      } else if (parseOptions && value.startsWith('--type=')) {
        type = value.slice('--type='.length);
      } else if (parseOptions && value === '--no-filters') {
        continue;
      } else if (parseOptions && value === '--stdin') {
        stdinMode = true;
      } else if (parseOptions && value === '--stdin-paths') {
        stdinPathsMode = true;
      } else if (parseOptions && value === '--literally') {
        return 'fatal: git hash-object --literally requires standard input, which is not available yet';
      } else if (parseOptions && value.startsWith('--path')) {
        return 'fatal: git hash-object --path requires standard input, which is not available yet';
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        paths.push(value);
      }
    }
    const supportedTypes: string[] = ['blob', 'tree', 'commit', 'tag'];
    if (!supportedTypes.includes(type)) {
      return `fatal: invalid object type "${type}"`;
    }
    if (stdinMode && stdinPathsMode) {
      return 'fatal: git hash-object cannot combine --stdin and --stdin-paths';
    }
    if (stdinMode && paths.length > 0) {
      return 'fatal: git hash-object --stdin cannot be combined with file paths';
    }
    if (stdinPathsMode && paths.length > 0) {
      return 'fatal: git hash-object --stdin-paths cannot be combined with file paths';
    }
    if (stdinMode) {
      try {
        return this.repositoryService.hashInput(
          this.currentPath,
          input,
          type,
          write
        );
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (stdinPathsMode) {
      const inputPaths: string[] = this.inputLines(input);
      if (inputPaths.length === 0) {
        return '';
      }
      try {
        return this.repositoryService.hashObject(
          this.currentPath,
          inputPaths,
          type,
          write
        ).join('\n');
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (paths.length === 0) {
      return 'usage: git hash-object [-w] [-t <type>] [--] <file>...';
    }
    try {
      return this.repositoryService.hashObject(
        this.currentPath,
        paths,
        type,
        write
      ).join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeCheckIgnore(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let noIndex: boolean = false;
    let quiet: boolean = false;
    let verbose: boolean = false;
    let stdinMode: boolean = false;
    let parseOptions: boolean = true;
    const paths: string[] = [];
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--no-index') {
        noIndex = true;
      } else if (parseOptions && value === '--quiet') {
        quiet = true;
      } else if (parseOptions && value === '--verbose') {
        verbose = true;
      } else if (parseOptions && value === '--stdin') {
        stdinMode = true;
      } else if (parseOptions && value.startsWith('-') &&
          value.length > 1 && !value.startsWith('--')) {
        for (const flag of value.slice(1)) {
          if (flag === 'q') {
            quiet = true;
          } else if (flag === 'v') {
            verbose = true;
          } else {
            return `error: unknown switch \`${flag}\``;
          }
        }
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        paths.push(value);
      }
    }
    if (quiet && verbose) {
      return 'fatal: cannot combine --quiet and --verbose';
    }
    if (stdinMode && paths.length > 0) {
      return 'fatal: cannot specify pathnames with --stdin';
    }
    if (stdinMode) {
      paths.push(...this.inputLines(input));
    }
    if (paths.length === 0) {
      return 'usage: git check-ignore [-q | -v] [--no-index] [--] <pathname>...';
    }
    try {
      const lines: string[] = this.repositoryService.checkIgnore(
        this.currentPath,
        paths,
        noIndex,
        verbose
      );
      return quiet ? '' : lines.join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeCatFile(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    if (args.length !== 2) {
      return 'usage: git cat-file (-t | -s | -e | -p | <type>) <object>';
    }
    const modeByOption: Record<string, string> = {
      '-t': 'type',
      '-s': 'size',
      '-e': 'exists',
      '-p': 'pretty'
    };
    const requestedMode: string = modeByOption[args[0]] ?? args[0];
    const supportedModes: string[] = [
      'type',
      'size',
      'exists',
      'pretty',
      'blob',
      'tree',
      'commit',
      'tag'
    ];
    if (!supportedModes.includes(requestedMode)) {
      return args[0].startsWith('-')
        ? `error: unknown option \`${args[0]}\``
        : `fatal: invalid object type "${args[0]}"`;
    }
    try {
      return this.repositoryService.catFile(
        this.currentPath,
        args[1],
        requestedMode
      );
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeListTree(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryListTreeOptions = {
      recursive: false,
      directoriesOnly: false,
      includeTrees: false,
      nameOnly: false,
      objectOnly: false,
      longFormat: false,
      fullName: false,
      fullTree: false,
      paths: []
    };
    let treeish: string = '';
    let parseOptions: boolean = true;
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--name-only' ||
          parseOptions && value === '--name-status') {
        options.nameOnly = true;
      } else if (parseOptions && value === '--object-only') {
        options.objectOnly = true;
      } else if (parseOptions && value === '--long') {
        options.longFormat = true;
      } else if (parseOptions && value === '--full-name') {
        options.fullName = true;
      } else if (parseOptions && value === '--full-tree') {
        options.fullTree = true;
      } else if (parseOptions && value.startsWith('-') &&
          value.length > 1 && !value.startsWith('--')) {
        for (const flag of value.slice(1)) {
          if (flag === 'r') {
            options.recursive = true;
          } else if (flag === 'd') {
            options.directoriesOnly = true;
          } else if (flag === 't') {
            options.includeTrees = true;
          } else if (flag === 'l') {
            options.longFormat = true;
          } else {
            return `error: unknown switch \`${flag}\``;
          }
        }
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else if (treeish.length === 0) {
        treeish = value;
      } else {
        options.paths.push(value);
      }
    }
    if (treeish.length === 0) {
      return 'usage: git ls-tree [<options>] <tree-ish> [<path>...]';
    }
    if (options.nameOnly && options.objectOnly) {
      return 'fatal: git ls-tree cannot combine --name-only and --object-only.';
    }
    try {
      return this.repositoryService.listTree(
        this.currentPath,
        treeish,
        options
      ).join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeShowRef(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryShowRefOptions = {
      heads: false,
      tags: false,
      includeHead: false,
      dereference: false,
      verify: false,
      quiet: false,
      hashOnly: false,
      abbreviation: 40,
      patterns: []
    };
    let excludeExisting: boolean = false;
    let excludePattern: string = '';
    let verificationMode: string = '';
    let parseOptions: boolean = true;
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--heads') {
        options.heads = true;
      } else if (parseOptions && value === '--tags') {
        options.tags = true;
      } else if (parseOptions && value === '--head') {
        options.includeHead = true;
      } else if (parseOptions &&
          (value === '-d' || value === '--dereference')) {
        options.dereference = true;
      } else if (parseOptions && value === '--verify') {
        options.verify = true;
        verificationMode = '--verify';
      } else if (parseOptions && value === '--exists') {
        options.verify = true;
        options.quiet = true;
        verificationMode = '--exists';
      } else if (parseOptions &&
          (value === '-q' || value === '--quiet')) {
        options.quiet = true;
      } else if (parseOptions &&
          (value === '-s' || value === '--hash')) {
        options.hashOnly = true;
      } else if (parseOptions && value.startsWith('--hash=')) {
        options.hashOnly = true;
        options.abbreviation =
          Number(value.slice('--hash='.length));
      } else if (parseOptions && value.startsWith('-s') &&
          value.length > 2) {
        options.hashOnly = true;
        options.abbreviation = Number(value.slice(2));
      } else if (parseOptions && value === '--abbrev') {
        options.abbreviation = 7;
      } else if (parseOptions && value.startsWith('--abbrev=')) {
        options.abbreviation =
          Number(value.slice('--abbrev='.length));
      } else if (parseOptions && value === '--exclude-existing') {
        excludeExisting = true;
      } else if (parseOptions &&
          value.startsWith('--exclude-existing=')) {
        excludeExisting = true;
        excludePattern = value.slice('--exclude-existing='.length);
      } else if (parseOptions && value.startsWith('-') &&
          value.length > 1 && !value.startsWith('--')) {
        for (const flag of value.slice(1)) {
          if (flag === 'd') {
            options.dereference = true;
          } else if (flag === 'q') {
            options.quiet = true;
          } else if (flag === 's') {
            options.hashOnly = true;
          } else {
            return `error: unknown switch \`${flag}\``;
          }
        }
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        options.patterns.push(value);
      }
    }
    if (!Number.isFinite(options.abbreviation) ||
        options.abbreviation < 1 ||
        options.abbreviation > 40 ||
        Math.floor(options.abbreviation) !== options.abbreviation) {
      return 'fatal: invalid object name abbreviation length';
    }
    if (excludeExisting && verificationMode.length > 0) {
      return `fatal: options '--exclude-existing' and '${verificationMode}' cannot be used together`;
    }
    if (options.verify && options.patterns.length === 0) {
      return 'usage: git show-ref --verify [--quiet] [--] <ref>...';
    }
    try {
      if (excludeExisting) {
        return this.repositoryService.excludeExistingRefs(
          this.currentPath,
          input,
          excludePattern
        ).join('\n');
      }
      return this.repositoryService.showRef(
        this.currentPath,
        options
      ).join('\n');
    } catch (error) {
      return options.quiet ? '' : `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeRevList(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryRevListOptions = {
      all: false,
      branches: false,
      tags: false,
      remotes: false,
      parents: false,
      count: false,
      reverse: false,
      firstParent: false,
      noMerges: false,
      merges: false,
      abbreviate: false,
      abbreviation: 7,
      maxCount: 4294967295,
      revisions: [],
      paths: []
    };
    let parseOptions: boolean = true;
    let negateRevisions: boolean = false;
    let stdinMode: boolean = false;
    let quiet: boolean = false;
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (!parseOptions) {
        options.paths.push(value);
      } else if (parseOptions && value === '--all') {
        options.all = true;
      } else if (parseOptions && value === '--branches') {
        options.branches = true;
      } else if (parseOptions && value === '--tags') {
        options.tags = true;
      } else if (parseOptions && value === '--remotes') {
        options.remotes = true;
      } else if (parseOptions && value === '--parents') {
        options.parents = true;
      } else if (parseOptions && value === '--count') {
        options.count = true;
      } else if (parseOptions && value === '--reverse') {
        options.reverse = true;
      } else if (parseOptions && value === '--first-parent') {
        options.firstParent = true;
      } else if (parseOptions && value === '--no-merges') {
        options.noMerges = true;
      } else if (parseOptions && value === '--merges') {
        options.merges = true;
      } else if (parseOptions && value === '--abbrev-commit') {
        options.abbreviate = true;
      } else if (parseOptions && value === '--no-abbrev-commit') {
        options.abbreviate = false;
      } else if (parseOptions && value === '--abbrev') {
        options.abbreviation = 7;
      } else if (parseOptions && value.startsWith('--abbrev=')) {
        options.abbreviation = Number(value.slice('--abbrev='.length));
      } else if (parseOptions &&
          (value === '-n' || value === '--max-count')) {
        if (index + 1 >= args.length) {
          return `error: option '${value}' requires a value`;
        }
        options.maxCount = Number(args[++index]);
      } else if (parseOptions && value.startsWith('--max-count=')) {
        options.maxCount =
          Number(value.slice('--max-count='.length));
      } else if (parseOptions && value.startsWith('-n') &&
          value.length > 2) {
        options.maxCount = Number(value.slice(2));
      } else if (parseOptions && value === '--not') {
        negateRevisions = !negateRevisions;
      } else if (parseOptions &&
          (value === '--quiet' || value === '-q')) {
        quiet = true;
      } else if (parseOptions && value === '--stdin') {
        stdinMode = true;
      } else if (parseOptions &&
          (value === '--objects' || value === '--objects-edge' ||
           value.startsWith('--bisect'))) {
        return `fatal: git rev-list ${value} requires native stdin or object enumeration support that is not enabled yet`;
      } else if (parseOptions && value.startsWith('-') &&
          !value.startsWith('^')) {
        return `error: unknown option \`${value}\``;
      } else {
        options.revisions.push(
          negateRevisions && !value.startsWith('^')
            ? `^${value}`
            : value
        );
      }
    }
    if (stdinMode) {
      let stdinPathsMode: boolean = false;
      for (const line of this.inputLines(input)) {
        if (line === '--') {
          stdinPathsMode = true;
        } else if (stdinPathsMode) {
          options.paths.push(line);
        } else {
          options.revisions.push(
            negateRevisions && !line.startsWith('^')
              ? `^${line}`
              : line
          );
        }
      }
    }
    if (!Number.isFinite(options.maxCount) ||
        options.maxCount < 0 ||
        options.maxCount > 4294967295 ||
        Math.floor(options.maxCount) !== options.maxCount) {
      return 'fatal: invalid --max-count value';
    }
    if (!Number.isFinite(options.abbreviation) ||
        options.abbreviation < 1 ||
        options.abbreviation > 40 ||
        Math.floor(options.abbreviation) !== options.abbreviation) {
      return 'fatal: invalid object name abbreviation length';
    }
    try {
      const lines: string[] =
        this.repositoryService.revList(this.currentPath, options);
      return quiet ? '' : lines.join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeMergeBase(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryMergeBaseOptions = {
      all: false,
      octopus: false,
      independent: false,
      revisions: []
    };
    let parseOptions: boolean = true;
    let isAncestorMode: boolean = false;
    let forkPointMode: boolean = false;
    for (const value of args) {
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '-a' || value === '--all')) {
        options.all = true;
      } else if (parseOptions && value === '--octopus') {
        options.octopus = true;
      } else if (parseOptions && value === '--independent') {
        options.independent = true;
      } else if (parseOptions && value === '--is-ancestor') {
        isAncestorMode = true;
      } else if (parseOptions && value === '--fork-point') {
        forkPointMode = true;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        options.revisions.push(value);
      }
    }
    if (forkPointMode) {
      if (isAncestorMode || options.octopus || options.independent) {
        this.lastCommandExitCode = 129;
        return 'fatal: git merge-base --fork-point cannot be combined with other graph modes';
      }
      if (options.revisions.length < 1 ||
          options.revisions.length > 2) {
        this.lastCommandExitCode = 129;
        return 'usage: git merge-base --fork-point <ref> [<commit>]';
      }
      try {
        const forkPoint: string =
          this.repositoryService.findForkPoint(
            this.currentPath,
            options.revisions[0],
            options.revisions.length === 2
              ? options.revisions[1]
              : 'HEAD'
          );
        this.lastCommandExitCode = forkPoint.length === 0 ? 1 : 0;
        return forkPoint;
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (isAncestorMode) {
      if (options.all || options.octopus || options.independent) {
        this.lastCommandExitCode = 128;
        return 'fatal: git merge-base --is-ancestor cannot be combined with other graph modes';
      }
      if (options.revisions.length !== 2) {
        this.lastCommandExitCode = 129;
        return 'usage: git merge-base --is-ancestor <commit> <commit>';
      }
      try {
        const ancestor: boolean = this.repositoryService.isAncestor(
          this.currentPath,
          options.revisions[0],
          options.revisions[1]
        );
        this.lastCommandExitCode = ancestor ? 0 : 1;
        return '';
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    const minimum: number = options.independent ? 1 : 2;
    if (options.revisions.length < minimum) {
      this.lastCommandExitCode = 129;
      return options.independent
        ? 'usage: git merge-base --independent <commit>...'
        : 'usage: git merge-base [-a | --all] [--octopus] <commit> <commit>...';
    }
    if (options.independent && options.octopus) {
      this.lastCommandExitCode = 128;
      return 'fatal: git merge-base cannot combine --independent and --octopus';
    }
    try {
      const lines: string[] = this.repositoryService.mergeBase(
        this.currentPath,
        options
      );
      this.lastCommandExitCode = lines.length === 0 ? 1 : 0;
      return lines.join('\n');
    } catch (error) {
      this.lastCommandExitCode = 128;
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeForEachRef(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const options: RepositoryForEachRefOptions = {
      count: 4294967295,
      format: '',
      sortKeys: [],
      patterns: [],
      excludes: [],
      pointsAt: '',
      merged: '',
      noMerged: '',
      contains: '',
      noContains: '',
      ignoreCase: false,
      includeRootRefs: false
    };
    let parseOptions: boolean = true;
    let stdinMode: boolean = false;
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions && value === '--count') {
        if (index + 1 >= args.length) {
          return 'error: option `count` requires a value';
        }
        options.count = Number(args[++index]);
      } else if (parseOptions && value.startsWith('--count=')) {
        options.count = Number(value.slice('--count='.length));
      } else if (parseOptions && value === '--format') {
        if (index + 1 >= args.length) {
          return 'error: option `format` requires a value';
        }
        options.format = args[++index];
      } else if (parseOptions && value.startsWith('--format=')) {
        options.format = value.slice('--format='.length);
      } else if (parseOptions && value === '--sort') {
        if (index + 1 >= args.length) {
          return 'error: option `sort` requires a value';
        }
        options.sortKeys.push(args[++index]);
      } else if (parseOptions && value.startsWith('--sort=')) {
        options.sortKeys.push(value.slice('--sort='.length));
      } else if (parseOptions && value === '--exclude') {
        if (index + 1 >= args.length) {
          return 'error: option `exclude` requires a value';
        }
        options.excludes.push(args[++index]);
      } else if (parseOptions && value.startsWith('--exclude=')) {
        options.excludes.push(value.slice('--exclude='.length));
      } else if (parseOptions && value === '--points-at') {
        if (index + 1 >= args.length) {
          return 'error: option `points-at` requires a value';
        }
        options.pointsAt = args[++index];
      } else if (parseOptions && value.startsWith('--points-at=')) {
        options.pointsAt = value.slice('--points-at='.length);
      } else if (parseOptions && value === '--merged') {
        options.merged = 'HEAD';
      } else if (parseOptions && value.startsWith('--merged=')) {
        options.merged = value.slice('--merged='.length);
      } else if (parseOptions && value === '--no-merged') {
        options.noMerged = 'HEAD';
      } else if (parseOptions && value.startsWith('--no-merged=')) {
        options.noMerged = value.slice('--no-merged='.length);
      } else if (parseOptions && value === '--contains') {
        options.contains = 'HEAD';
      } else if (parseOptions && value.startsWith('--contains=')) {
        options.contains = value.slice('--contains='.length);
      } else if (parseOptions && value === '--no-contains') {
        options.noContains = 'HEAD';
      } else if (parseOptions && value.startsWith('--no-contains=')) {
        options.noContains = value.slice('--no-contains='.length);
      } else if (parseOptions && value === '--ignore-case') {
        options.ignoreCase = true;
      } else if (parseOptions && value === '--include-root-refs') {
        options.includeRootRefs = true;
      } else if (parseOptions && value === '--stdin') {
        stdinMode = true;
      } else if (parseOptions &&
          (value === '--shell' || value === '--perl' ||
           value === '--python' || value === '--tcl' ||
           value === '--start-after' ||
           value.startsWith('--start-after='))) {
        return `fatal: git for-each-ref ${value} is not supported by the native formatter yet`;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        options.patterns.push(value);
      }
    }
    if (stdinMode) {
      options.patterns.push(...this.inputLines(input));
    }
    if (!Number.isFinite(options.count) ||
        options.count < 0 ||
        options.count > 4294967295 ||
        Math.floor(options.count) !== options.count) {
      return 'fatal: invalid --count value';
    }
    try {
      return this.repositoryService.forEachRef(
        this.currentPath,
        options
      ).join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeSymbolicRef(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let quiet: boolean = false;
    let shortName: boolean = false;
    let recurse: boolean = true;
    let deleteReference: boolean = false;
    let message: string = '';
    let parseOptions: boolean = true;
    const values: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '-q' || value === '--quiet')) {
        quiet = true;
      } else if (parseOptions && value === '--short') {
        shortName = true;
      } else if (parseOptions && value === '--no-recurse') {
        recurse = false;
      } else if (parseOptions &&
          (value === '-d' || value === '--delete')) {
        deleteReference = true;
      } else if (parseOptions &&
          (value === '-m' || value === '--message')) {
        if (index + 1 >= args.length) {
          return `error: option '${value}' requires a value`;
        }
        message = args[++index];
      } else if (parseOptions && value.startsWith('--message=')) {
        message = value.slice('--message='.length);
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        values.push(value);
      }
    }
    if (deleteReference) {
      if (values.length !== 1) {
        return 'usage: git symbolic-ref --delete [-q] <name>';
      }
      const operation: RepositoryOperation =
        this.repositoryService.updateSymbolicRef(
          this.currentPath,
          values[0],
          '',
          true,
          message
        );
      return operation.success ? '' : `fatal: ${operation.error}`;
    }
    if (values.length === 1) {
      try {
        return this.repositoryService.symbolicRef(
          this.currentPath,
          values[0],
          shortName,
          recurse
        );
      } catch (error) {
        return quiet ? '' : `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (values.length === 2 && !shortName) {
      const operation: RepositoryOperation =
        this.repositoryService.updateSymbolicRef(
          this.currentPath,
          values[0],
          values[1],
          false,
          message
        );
      return operation.success ? '' : `fatal: ${operation.error}`;
    }
    return 'usage: git symbolic-ref [-q] [--short] [--no-recurse] <name> [<ref>]';
  }

  private nativeUpdateRef(args: string[], input: string): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let deleteReference: boolean = false;
    let noDeref: boolean = false;
    let createReflog: boolean = false;
    let stdinMode: boolean = false;
    let nullTerminated: boolean = false;
    let batchUpdates: boolean = false;
    let message: string = '';
    let parseOptions: boolean = true;
    const values: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '-d' || value === '--delete')) {
        deleteReference = true;
      } else if (parseOptions && value === '--no-deref') {
        noDeref = true;
      } else if (parseOptions &&
          (value === '-m' || value === '--message')) {
        if (index + 1 >= args.length) {
          return `error: option '${value}' requires a value`;
        }
        message = args[++index];
      } else if (parseOptions && value.startsWith('--message=')) {
        message = value.slice('--message='.length);
      } else if (parseOptions && value === '--create-reflog') {
        createReflog = true;
      } else if (parseOptions && value === '--stdin') {
        stdinMode = true;
      } else if (parseOptions && value === '-z') {
        nullTerminated = true;
      } else if (parseOptions && value === '--batch-updates') {
        batchUpdates = true;
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        values.push(value);
      }
    }
    if (stdinMode) {
      if (deleteReference || values.length > 0) {
        return 'usage: git update-ref [-m <reason>] [--no-deref] [--create-reflog] --stdin';
      }
      const operation: RepositoryOperation =
        this.repositoryService.updateRefs(
          this.currentPath,
          input,
          noDeref,
          createReflog,
          message,
          nullTerminated,
          batchUpdates
        );
      return operation.success
        ? operation.output.join('\n')
        : `fatal: ${operation.error}`;
    }
    if (nullTerminated || batchUpdates) {
      return 'usage: git update-ref [<options>] --stdin [-z]';
    }
    if (deleteReference) {
      if (values.length < 1 || values.length > 2) {
        return 'usage: git update-ref [-m <reason>] -d <ref> [<old-oid>]';
      }
    } else if (values.length < 2 || values.length > 3) {
      return 'usage: git update-ref [-m <reason>] [--no-deref] <ref> <new-oid> [<old-oid>]';
    }
    const operation: RepositoryOperation =
      this.repositoryService.updateRef(
        this.currentPath,
        values[0],
        deleteReference ? '' : values[1],
        deleteReference ? values[1] ?? '' : values[2] ?? '',
        deleteReference,
        noDeref,
        message,
        createReflog
      );
    return operation.success ? '' : `fatal: ${operation.error}`;
  }

  private nativeTag(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let annotated: boolean = false;
    let deleteMode: boolean = false;
    let force: boolean = false;
    let listMode: boolean = false;
    let cleanup: string = 'strip';
    let messageProvided: boolean = false;
    let parseOptions: boolean = true;
    const messages: string[] = [];
    const values: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '-a' || value === '--annotate')) {
        annotated = true;
      } else if (parseOptions &&
          (value === '-d' || value === '--delete')) {
        deleteMode = true;
      } else if (parseOptions &&
          (value === '-f' || value === '--force')) {
        force = true;
      } else if (parseOptions &&
          (value === '-l' || value === '--list')) {
        listMode = true;
      } else if (parseOptions &&
          (value === '-m' || value === '--message')) {
        if (index + 1 >= args.length) {
          return 'error: switch `m` requires a value';
        }
        messages.push(args[index + 1]);
        messageProvided = true;
        annotated = true;
        index++;
      } else if (parseOptions && value.startsWith('--message=')) {
        messages.push(value.slice('--message='.length));
        messageProvided = true;
        annotated = true;
      } else if (parseOptions &&
          (value === '-F' || value === '--file')) {
        if (index + 1 >= args.length) {
          return 'error: switch `F` requires a value';
        }
        const filePath: string = args[++index];
        const file: ShellFileReadResult = this.readShellFile(filePath);
        if (!file.success) {
          return `fatal: could not read tag message file '${filePath}': ${file.error}`;
        }
        messages.push(file.content);
        messageProvided = true;
        annotated = true;
      } else if (parseOptions && value.startsWith('--file=')) {
        const filePath: string = value.slice('--file='.length);
        const file: ShellFileReadResult = this.readShellFile(filePath);
        if (!file.success) {
          return `fatal: could not read tag message file '${filePath}': ${file.error}`;
        }
        messages.push(file.content);
        messageProvided = true;
        annotated = true;
      } else if (parseOptions && value === '--cleanup') {
        if (index + 1 >= args.length) {
          return 'error: option `cleanup` requires a value';
        }
        cleanup = args[++index];
        if (!this.validTagCleanup(cleanup)) {
          return `fatal: Invalid cleanup mode ${cleanup}`;
        }
      } else if (parseOptions && value.startsWith('--cleanup=')) {
        cleanup = value.slice('--cleanup='.length);
        if (!this.validTagCleanup(cleanup)) {
          return `fatal: Invalid cleanup mode ${cleanup}`;
        }
      } else if (parseOptions && value.startsWith('-')) {
        return `error: unknown option \`${value}\``;
      } else {
        values.push(value);
      }
    }

    if (deleteMode) {
      if (annotated || listMode || force) {
        return 'error: tag deletion cannot be combined with create or list options';
      }
      if (values.length === 0) {
        return 'fatal: tag -d requires a tag name';
      }
      const operation: RepositoryOperation =
        this.repositoryService.deleteTags(this.currentPath, values);
      if (!operation.success) {
        return `error: ${operation.error}`;
      }
      return values.map(
        (name: string): string => `Deleted tag '${name}'.`
      ).join('\n');
    }

    if (listMode || (values.length === 0 && !annotated && !force)) {
      try {
        return this.repositoryService.tags(
          this.currentPath,
          values
        ).join('\n');
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    if (values.length === 0 || values.length > 2) {
      return 'usage: git tag [-a] [-f] [-m <message>] [-F <file>] <tagname> [<commit>]';
    }
    if (annotated && !messageProvided) {
      this.tagEditor = {
        name: values[0],
        target: values[1] ?? '',
        force,
        cleanup,
        lines: []
      };
      return `Editing tag message for '${values[0]}'. Use :wq to save or :q! to cancel.`;
    }
    const message: string =
      this.cleanTagMessage(messages.join('\n\n'), cleanup);
    if (annotated && message.trim().length === 0) {
      this.lastCommandExitCode = 128;
      return 'fatal: no tag message?';
    }
    const operation: RepositoryOperation =
      this.repositoryService.createTag(
        this.currentPath,
        values[0],
        values[1] ?? '',
        force,
        annotated,
        message
      );
    return operation.success ? '' : `fatal: ${operation.error}`;
  }

  private editTagMessage(rawLine: string): string {
    const previousExitCode: number = this.lastCommandExitCode;
    this.lastCommandExitCode = 0;
    this.lastExpansionExitCode = previousExitCode;
    const session: TagEditorSession | undefined = this.tagEditor;
    if (session === undefined) {
      return '';
    }
    const editorCommand: string = rawLine.trim();
    if (editorCommand === ':q!' || editorCommand === ':cq') {
      this.tagEditor = undefined;
      return 'Tag creation cancelled.';
    }
    if (editorCommand !== ':wq') {
      const normalized: string =
        rawLine.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
      for (const line of normalized.split('\n')) {
        session.lines.push(line);
      }
      return '';
    }

    const message: string =
      this.cleanTagMessage(session.lines.join('\n'), session.cleanup);
    this.tagEditor = undefined;
    if (message.trim().length === 0) {
      this.lastCommandExitCode = 128;
      return 'fatal: no tag message?';
    }
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    const operation: RepositoryOperation =
      this.repositoryService.createTag(
        this.currentPath,
        session.name,
        session.target,
        session.force,
        true,
        message
      );
    if (!operation.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${operation.error}`;
    }
    return '';
  }

  private validTagCleanup(cleanup: string): boolean {
    return cleanup === 'strip' ||
      cleanup === 'whitespace' ||
      cleanup === 'verbatim';
  }

  private cleanTagMessage(message: string, cleanup: string): string {
    const normalized: string =
      message.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    if (cleanup === 'verbatim') {
      return normalized;
    }
    const lines: string[] = [];
    for (const rawLine of normalized.split('\n')) {
      if (cleanup === 'strip' && /^[ \t]*#/.test(rawLine)) {
        continue;
      }
      const line: string = rawLine.trimEnd();
      if (line.length === 0 &&
          (lines.length === 0 || lines[lines.length - 1].length === 0)) {
        continue;
      }
      lines.push(line);
    }
    while (lines.length > 0 &&
        lines[lines.length - 1].length === 0) {
      lines.pop();
    }
    return lines.join('\n');
  }

  private nativeConfig(args: string[]): string {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    const usage: string =
      'usage: git config [--local | --global | --system | --file <file>] ' +
      '[--includes | --no-includes] ' +
      '[--bool | --int | --bool-or-int | --bool-or-str | --path | ' +
      '--expiry-date | --type <type>] [--default <value>] ' +
      '[--list | --get key | --get-all key | --add key value | ' +
      '--unset key | --unset-all key | key [value]]';
    let scope: string = 'all';
    let scopeSelected: boolean = false;
    let explicitFile: string = '';
    let includes: boolean = true;
    let includesSelected: boolean = false;
    let mode: string = '';
    let configType: string = '';
    let defaultValue: string = '';
    let defaultSelected: boolean = false;
    let parseOptions: boolean = true;
    const values: string[] = [];
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (parseOptions && value === '--') {
        parseOptions = false;
      } else if (parseOptions &&
          (value === '--local' ||
           value === '--global' ||
           value === '--system')) {
        const selectedScope: string = value.slice(2);
        if (scopeSelected && scope !== selectedScope) {
          this.lastCommandExitCode = 129;
          return 'error: only one config scope may be selected';
        }
        scope = selectedScope;
        scopeSelected = true;
      } else if (parseOptions &&
          (value === '--file' || value === '-f')) {
        if (index + 1 >= args.length) {
          this.lastCommandExitCode = 129;
          return `error: option '${value}' requires a value`;
        }
        if (scopeSelected && scope !== 'file') {
          this.lastCommandExitCode = 129;
          return 'error: only one config scope may be selected';
        }
        scope = 'file';
        scopeSelected = true;
        explicitFile = this.resolvePath(args[++index]);
      } else if (parseOptions && value.startsWith('--file=')) {
        if (scopeSelected && scope !== 'file') {
          this.lastCommandExitCode = 129;
          return 'error: only one config scope may be selected';
        }
        scope = 'file';
        scopeSelected = true;
        explicitFile = this.resolvePath(value.slice('--file='.length));
      } else if (parseOptions && value === '--includes') {
        includes = true;
        includesSelected = true;
      } else if (parseOptions && value === '--no-includes') {
        includes = false;
        includesSelected = true;
      } else if (parseOptions &&
          (value === '--list' ||
           value === '-l' ||
           value === '--get' ||
           value === '--get-all' ||
           value === '--add' ||
           value === '--unset' ||
           value === '--unset-all')) {
        const selectedMode: string =
          value === '-l' ? '--list' : value;
        if (mode.length > 0 && mode !== selectedMode) {
          this.lastCommandExitCode = 129;
          return 'error: only one config action may be selected';
        }
        mode = selectedMode;
      } else if (parseOptions &&
          (value === '--bool' ||
           value === '--int' ||
           value === '--bool-or-int' ||
           value === '--bool-or-str' ||
           value === '--path' ||
           value === '--expiry-date')) {
        const selectedType: string = value.slice(2);
        if (configType.length > 0 && configType !== selectedType) {
          this.lastCommandExitCode = 129;
          return 'error: only one config type may be selected';
        }
        configType = selectedType;
      } else if (parseOptions && value === '--type') {
        if (index + 1 >= args.length) {
          this.lastCommandExitCode = 129;
          return 'error: option \'--type\' requires a value';
        }
        const selectedType: string = args[++index];
        if (!this.validConfigType(selectedType)) {
          this.lastCommandExitCode = 129;
          return `error: invalid type specifier '${selectedType}'`;
        }
        if (configType.length > 0 && configType !== selectedType) {
          this.lastCommandExitCode = 129;
          return 'error: only one config type may be selected';
        }
        configType = selectedType;
      } else if (parseOptions && value.startsWith('--type=')) {
        const selectedType: string = value.slice('--type='.length);
        if (!this.validConfigType(selectedType)) {
          this.lastCommandExitCode = 129;
          return `error: invalid type specifier '${selectedType}'`;
        }
        if (configType.length > 0 && configType !== selectedType) {
          this.lastCommandExitCode = 129;
          return 'error: only one config type may be selected';
        }
        configType = selectedType;
      } else if (parseOptions && value === '--default') {
        if (index + 1 >= args.length || defaultSelected) {
          this.lastCommandExitCode = 129;
          return usage;
        }
        defaultValue = args[++index];
        defaultSelected = true;
      } else if (parseOptions && value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      } else {
        values.push(value);
      }
    }
    if (!includesSelected) {
      includes = !scopeSelected;
    }

    if (mode === '--list') {
      if (values.length !== 0) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      try {
        const entries: RepositoryConfigEntry[] =
          this.repositoryService.readConfig(
            this.currentPath,
            scope,
            includes,
            explicitFile
          );
        this.lastCommandExitCode = 0;
        return entries.map(
          (entry: RepositoryConfigEntry): string =>
            `${entry.key}\t${this.normalizeConfigValue(
              entry.value,
              configType,
              false
            )}`
        ).join('\n');
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    const readMode: boolean =
      mode === '--get' ||
      mode === '--get-all' ||
      (mode.length === 0 && values.length === 1);
    if (readMode) {
      if (values.length !== 1) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      try {
        const key: string = this.normalizeConfigKey(values[0]);
        const matches: RepositoryConfigEntry[] =
          this.repositoryService.readConfig(
            this.currentPath,
            scope,
            includes,
            explicitFile
          ).filter(
            (entry: RepositoryConfigEntry): boolean =>
              entry.key === key
          );
        if (matches.length === 0) {
          if (defaultSelected) {
            this.lastCommandExitCode = 0;
            return this.normalizeConfigValue(
              defaultValue,
              configType,
              false
            );
          }
          this.lastCommandExitCode = 1;
          return '';
        }
        this.lastCommandExitCode = 0;
        if (mode === '--get-all') {
          return matches.map(
            (entry: RepositoryConfigEntry): string =>
              this.normalizeConfigValue(
                entry.value,
                configType,
                false
              )
          ).join('\n');
        }
        return this.normalizeConfigValue(
          matches[matches.length - 1].value,
          configType,
          false
        );
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    if (defaultSelected) {
      this.lastCommandExitCode = 129;
      return usage;
    }
    const writeScope: string = scopeSelected ? scope : 'local';
    if (mode === '--unset' || mode === '--unset-all') {
      if (values.length !== 1) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      const operation: RepositoryOperation =
        this.repositoryService.unsetConfig(
          this.currentPath,
          values[0],
          writeScope,
          mode === '--unset-all',
          explicitFile
        );
      if (!operation.success) {
        this.lastCommandExitCode = 5;
        return `error: ${operation.error}`;
      }
      this.lastCommandExitCode = 0;
      return '';
    }

    if (mode === '--add' ||
        (mode.length === 0 && values.length === 2)) {
      if (values.length !== 2) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      try {
        const operation: RepositoryOperation =
          this.repositoryService.setConfig(
            this.currentPath,
            values[0],
            this.normalizeConfigValue(values[1], configType, true),
            writeScope,
            mode === '--add',
            explicitFile
          );
        if (!operation.success) {
          this.lastCommandExitCode = 5;
          return `error: ${operation.error}`;
        }
        this.lastCommandExitCode = 0;
        return '';
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    this.lastCommandExitCode = 129;
    return usage;
  }

  private nativeReflog(args: string[]): string {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    const usage: string =
      'usage: git reflog [show] [--max-count=<count>] ' +
      '[--skip=<count>] [--since=<date>] [--until=<date>] ' +
      '[--format=<format>] [<ref>]\n' +
      '   or: git reflog list\n' +
      '   or: git reflog exists <ref>\n' +
      '   or: git reflog write <ref> <old-oid> <new-oid> <message>\n' +
      '   or: git reflog delete [--rewrite] [--updateref] ' +
      '[--dry-run] [--verbose] <ref>@{<index>}...\n' +
      '   or: git reflog expire [--expire=<time>] ' +
      '[--expire-unreachable=<time>] [--rewrite] [--updateref] ' +
      '[--stale-fix] [--dry-run] [--verbose] ' +
      '[--all [--single-worktree] | <refs>...]\n' +
      '   or: git reflog drop [--all [--single-worktree] | <refs>...]';
    let subcommand: string = 'show';
    let values: string[] = args;
    if (args.length > 0 && [
      'show',
      'list',
      'exists',
      'write',
      'delete',
      'drop',
      'expire'
    ].includes(args[0])) {
      subcommand = args[0];
      values = args.slice(1);
    }
    if (subcommand === 'expire') {
      let expire: string = '';
      let expireUnreachable: string = '';
      let rewrite: boolean = false;
      let updateRef: boolean = false;
      let staleFix: boolean = false;
      let dryRun: boolean = false;
      let verbose: boolean = false;
      let all: boolean = false;
      let singleWorktree: boolean = false;
      let parseOptions: boolean = true;
      const refs: string[] = [];
      const readOptionValue = (index: number): string | undefined => {
        if (index + 1 >= values.length) {
          return undefined;
        }
        return values[index + 1];
      };
      for (let index = 0; index < values.length; index++) {
        const value: string = values[index];
        if (parseOptions && value === '--') {
          parseOptions = false;
        } else if (parseOptions && value === '--rewrite') {
          rewrite = true;
        } else if (parseOptions && value === '--no-rewrite') {
          rewrite = false;
        } else if (parseOptions && value === '--updateref') {
          updateRef = true;
        } else if (parseOptions && value === '--no-updateref') {
          updateRef = false;
        } else if (parseOptions && value === '--stale-fix') {
          staleFix = true;
        } else if (parseOptions && value === '--no-stale-fix') {
          staleFix = false;
        } else if (parseOptions &&
            (value === '--dry-run' || value === '-n')) {
          dryRun = true;
        } else if (parseOptions && value === '--no-dry-run') {
          dryRun = false;
        } else if (parseOptions && value === '--verbose') {
          verbose = true;
        } else if (parseOptions && value === '--no-verbose') {
          verbose = false;
        } else if (parseOptions && value === '--all') {
          all = true;
        } else if (parseOptions && value === '--no-all') {
          all = false;
        } else if (parseOptions && value === '--single-worktree') {
          singleWorktree = true;
        } else if (parseOptions && value === '--no-single-worktree') {
          singleWorktree = false;
        } else if (parseOptions && value === '--expire') {
          const optionValue: string | undefined = readOptionValue(index);
          if (optionValue === undefined) {
            this.lastCommandExitCode = 129;
            return usage;
          }
          expire = optionValue;
          index++;
        } else if (parseOptions &&
            value.startsWith('--expire=')) {
          expire = value.slice('--expire='.length);
        } else if (parseOptions &&
            value === '--expire-unreachable') {
          const optionValue: string | undefined = readOptionValue(index);
          if (optionValue === undefined) {
            this.lastCommandExitCode = 129;
            return usage;
          }
          expireUnreachable = optionValue;
          index++;
        } else if (parseOptions &&
            value.startsWith('--expire-unreachable=')) {
          expireUnreachable =
            value.slice('--expire-unreachable='.length);
        } else if (parseOptions && value.startsWith('-')) {
          this.lastCommandExitCode = 129;
          return usage;
        } else {
          refs.push(value);
        }
      }
      if (all && refs.length > 0) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      if (!all && refs.length === 0) {
        refs.push('HEAD');
      }
      const operation: RepositoryOperation =
        this.repositoryService.expireReflogs(
          this.currentPath,
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
      if (!operation.success) {
        this.lastCommandExitCode = 1;
        return `error: ${operation.error}`;
      }
      this.lastCommandExitCode = 0;
      return operation.output.join('\n');
    }
    if (subcommand === 'list') {
      if (values.length > 0) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      try {
        const reflogs: string[] =
          this.repositoryService.listReflogs(this.currentPath);
        this.lastCommandExitCode = 0;
        return reflogs.join('\n');
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (subcommand === 'exists') {
      const refs: string[] = values.filter(
        (value: string): boolean =>
          value !== '--' && value !== '--end-of-options'
      );
      if (refs.length !== 1) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      try {
        const exists: boolean =
          this.repositoryService.reflogExists(
            this.currentPath,
            refs[0]
          );
        this.lastCommandExitCode = exists ? 0 : 1;
        return '';
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (subcommand === 'write') {
      const writeValues: string[] =
        values.length > 0 && values[0] === '--'
          ? values.slice(1)
          : values;
      if (writeValues.length !== 4) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      const operation: RepositoryOperation =
        this.repositoryService.writeReflog(
          this.currentPath,
          writeValues[0],
          writeValues[1],
          writeValues[2],
          writeValues[3]
        );
      if (!operation.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${operation.error}`;
      }
      this.lastCommandExitCode = 0;
      return operation.output.join('\n');
    }
    if (subcommand === 'delete') {
      let rewrite: boolean = false;
      let updateRef: boolean = false;
      let dryRun: boolean = false;
      let verbose: boolean = false;
      let parseOptions: boolean = true;
      const selectors: string[] = [];
      for (const value of values) {
        if (parseOptions && value === '--') {
          parseOptions = false;
        } else if (parseOptions && value === '--rewrite') {
          rewrite = true;
        } else if (parseOptions && value === '--updateref') {
          updateRef = true;
        } else if (parseOptions &&
          (value === '--dry-run' || value === '-n')) {
          dryRun = true;
        } else if (parseOptions && value === '--verbose') {
          verbose = true;
        } else if (parseOptions && value.startsWith('-')) {
          this.lastCommandExitCode = 129;
          return usage;
        } else {
          selectors.push(value);
        }
      }
      if (selectors.length === 0) {
        this.lastCommandExitCode = 1;
        return 'error: no reflog specified to delete';
      }
      const operation: RepositoryOperation =
        this.repositoryService.deleteReflogEntries(
          this.currentPath,
          selectors,
          rewrite,
          updateRef,
          dryRun,
          verbose
        );
      if (!operation.success) {
        this.lastCommandExitCode = 1;
        return `error: ${operation.error}`;
      }
      this.lastCommandExitCode = 0;
      return operation.output.join('\n');
    }
    if (subcommand === 'drop') {
      let all: boolean = false;
      let singleWorktree: boolean = false;
      let parseOptions: boolean = true;
      const refs: string[] = [];
      for (const value of values) {
        if (parseOptions && value === '--') {
          parseOptions = false;
        } else if (parseOptions && value === '--all') {
          all = true;
        } else if (parseOptions && value === '--single-worktree') {
          singleWorktree = true;
        } else if (parseOptions && value.startsWith('-')) {
          this.lastCommandExitCode = 129;
          return usage;
        } else {
          refs.push(value);
        }
      }
      if (all && refs.length > 0) {
        this.lastCommandExitCode = 129;
        return usage;
      }
      const operation: RepositoryOperation =
        this.repositoryService.dropReflogs(
          this.currentPath,
          refs,
          all,
          singleWorktree
        );
      if (!operation.success) {
        this.lastCommandExitCode = 1;
        return `error: ${operation.error}`;
      }
      this.lastCommandExitCode = 0;
      return operation.output.join('\n');
    }

    return this.nativeReflogShow(values, false);
  }

  private nativeListRemote(
    args: string[],
    credentials?: RepositoryCredentials
  ): string {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    let heads: boolean = false;
    let tags: boolean = false;
    let refsOnly: boolean = false;
    let symref: boolean = false;
    let exitCode: boolean = false;
    let getUrl: boolean = false;
    let remote: string = '';
    const patterns: string[] = [];
    for (const value of args) {
      if (value === '--heads' || value === '-h') {
        heads = true;
      } else if (value === '--tags' || value === '-t') {
        tags = true;
      } else if (value === '--refs') {
        refsOnly = true;
      } else if (value === '--symref') {
        symref = true;
      } else if (value === '--exit-code') {
        exitCode = true;
      } else if (value === '--get-url') {
        getUrl = true;
      } else if (value === '--quiet' || value === '-q') {
        continue;
      } else if (value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      } else if (remote.length === 0) {
        remote = value;
      } else {
        patterns.push(value);
      }
    }
    if (remote.length === 0) {
      remote = 'origin';
    }

    let url: string = remote;
    if (!this.isRemoteUrl(remote)) {
      try {
        url = this.repositoryService.getRemoteUrl(
          this.currentPath,
          remote,
          false
        );
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: '${remote}' does not appear to be a git repository\n${this.errorMessage(error)}`;
      }
    }
    if (getUrl) {
      return url;
    }

    const advertisement: RepositoryRemoteAdvertisement =
      this.repositoryService.listRemoteReferences(
        url,
        heads,
        tags,
        refsOnly,
        patterns,
        credentials
      );
    this.recordNetworkResponse(advertisement.responseCode, url);
    if (!advertisement.success) {
      this.lastCommandExitCode = 128;
      return `fatal: unable to access '${this.redactRemoteUrl(url)}': ${advertisement.error}`;
    }
    if (exitCode && advertisement.references.length === 0) {
      this.lastCommandExitCode = 2;
      return '';
    }
    const lines: string[] = [];
    if (symref &&
        advertisement.headTarget.length > 0 &&
        advertisement.references.some(
          (reference: RepositoryRemoteReference): boolean =>
            reference.name === 'HEAD'
        )) {
      lines.push(`ref: ${advertisement.headTarget}\tHEAD`);
    }
    lines.push(...advertisement.references.map(
      (reference: RepositoryRemoteReference): string =>
        `${reference.objectId}\t${reference.name}`
    ));
    return lines.join('\n');
  }

  private async nativeFetch(
    args: string[],
    credentials?: RepositoryCredentials
  ): Promise<string> {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    let remoteName: string = 'origin';
    let remoteSet: boolean = false;
    for (const value of args) {
      if (value === '--quiet' || value === '-q' ||
          value === '--verbose' || value === '-v') {
        continue;
      }
      if (value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      }
      if (remoteSet) {
        this.lastCommandExitCode = 129;
        return 'usage: git fetch [<remote>]';
      }
      remoteName = value;
      remoteSet = true;
    }

    const remoteUrl: string =
      this.networkRemoteUrl(remoteName, false);
    const operation: RepositoryOperation =
      await this.repositoryService.fetch(
        this.currentPath,
        remoteName,
        credentials
      );
    this.recordNetworkResponse(operation.responseCode, remoteUrl);
    if (!operation.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${operation.error}`;
    }
    this.lastCommandExitCode = 0;
    return operation.output.join('\n');
  }

  private async nativePush(
    args: string[],
    credentials?: RepositoryCredentials
  ): Promise<string> {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }

    let force: boolean = false;
    let setUpstream: boolean = false;
    let quiet: boolean = false;
    const values: string[] = [];
    for (const value of args) {
      if (value === '--force' || value === '-f') {
        force = true;
      } else if (value === '--set-upstream' || value === '-u') {
        setUpstream = true;
      } else if (value === '--quiet' || value === '-q') {
        quiet = true;
      } else if (value === '--verbose' || value === '-v') {
        continue;
      } else if (value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      } else {
        values.push(value);
      }
    }
    if (values.length > 2) {
      this.lastCommandExitCode = 129;
      return 'usage: git push [-u] [-f] [<repository> [<refspec>]]';
    }

    const snapshot: RepositorySnapshot | undefined =
      this.currentRepository();
    if (snapshot === undefined || !snapshot.valid) {
      this.lastCommandExitCode = 128;
      return 'fatal: not a git repository (or any of the parent directories): .git';
    }
    if (snapshot.detached || snapshot.branch.length === 0) {
      this.lastCommandExitCode = 128;
      return 'fatal: You are not currently on a branch.';
    }

    const config: RepositoryConfigEntry[] =
      this.repositoryService.readConfig(this.currentPath, 'all', true);
    const configuredRemote: string =
      this.configValue(
        config,
        `branch.${snapshot.branch}.remote`
      );
    const configuredMerge: string =
      this.configValue(
        config,
        `branch.${snapshot.branch}.merge`
      );
    const remoteName: string =
      values[0] ?? (configuredRemote.length > 0
        ? configuredRemote
        : 'origin');
    const configuredBranch: string =
      configuredMerge.startsWith('refs/heads/')
        ? configuredMerge.slice('refs/heads/'.length)
        : configuredMerge;
    const refspec: string =
      values[1] ?? `${snapshot.branch}:${configuredBranch.length > 0
        ? configuredBranch
        : snapshot.branch}`;
    const separator: number = refspec.indexOf(':');
    const sourceName: string =
      separator < 0 ? refspec : refspec.slice(0, separator);
    const destinationName: string =
      separator < 0 ? refspec : refspec.slice(separator + 1);
    if (destinationName.length === 0) {
      this.lastCommandExitCode = 129;
      return 'error: destination refspec is empty';
    }
    const destinationRef: string =
      destinationName.startsWith('refs/')
        ? destinationName
        : `refs/heads/${destinationName}`;
    const zeroObjectId: string = '0'.repeat(40);
    let newObjectId: string = zeroObjectId;
    if (sourceName.length > 0) {
      if (sourceName === 'HEAD' ||
          sourceName === snapshot.branch ||
          sourceName === `refs/heads/${snapshot.branch}`) {
        newObjectId = snapshot.head;
      } else {
        const sourceRef: string =
          sourceName.startsWith('refs/')
            ? sourceName
            : `refs/heads/${sourceName}`;
        newObjectId = this.referenceObjectId(
          this.currentPath,
          sourceRef
        );
      }
      if (newObjectId.length === 0) {
        this.lastCommandExitCode = 128;
        return `error: src refspec ${sourceName} does not match any.`;
      }
    }
    if (newObjectId.length === 0) {
      this.lastCommandExitCode = 128;
      return `error: src refspec ${sourceName} does not match any.`;
    }

    const update: RepositoryRemotePushUpdate = {
      oldObjectId: '',
      newObjectId,
      name: destinationRef
    };
    const remoteUrl: string =
      this.networkRemoteUrl(remoteName, true);
    const operation: RepositoryOperation =
      await this.repositoryService.push(
        this.currentPath,
        remoteName,
        [update],
        force,
        credentials
      );
    this.recordNetworkResponse(operation.responseCode, remoteUrl);
    if (!operation.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${operation.error}`;
    }

    if (setUpstream && sourceName.length > 0 &&
        destinationRef.startsWith('refs/heads/')) {
      const branchName: string =
        sourceName.startsWith('refs/heads/')
          ? sourceName.slice('refs/heads/'.length)
          : sourceName;
      const remoteConfig: RepositoryOperation =
        this.repositoryService.setConfig(
          this.currentPath,
          `branch.${branchName}.remote`,
          remoteName,
          'local',
          false
        );
      if (!remoteConfig.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${remoteConfig.error}`;
      }
      const mergeConfig: RepositoryOperation =
        this.repositoryService.setConfig(
          this.currentPath,
          `branch.${branchName}.merge`,
          destinationRef,
          'local',
          false
        );
      if (!mergeConfig.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${mergeConfig.error}`;
      }
    }

    if (quiet) {
      this.lastCommandExitCode = 0;
      return '';
    }
    const displayDestination: string =
      destinationRef.startsWith('refs/heads/')
        ? destinationRef.slice('refs/heads/'.length)
        : destinationRef;
    const displaySource: string =
      sourceName.startsWith('refs/heads/')
        ? sourceName.slice('refs/heads/'.length)
        : sourceName;
    const lines: string[] = [
      `To ${this.redactRemoteUrl(remoteUrl)}`
    ];
    if (sourceName.length === 0) {
      lines.push(` - [deleted] ${displayDestination}`);
    } else if (update.oldObjectId.length === 0 ||
        update.oldObjectId === zeroObjectId) {
      lines.push(
        ` * [new branch] ${displaySource} -> ${displayDestination}`
      );
    } else {
      const updateLabel: string = force
        ? '[forced update]'
        : `${this.shortObjectId(update.oldObjectId)}..` +
          `${this.shortObjectId(newObjectId)}`;
      lines.push(
        `   ${updateLabel} ${displaySource} -> ${displayDestination}`
      );
    }
    for (const line of operation.output) {
      if (line.startsWith('remote:')) {
        lines.push(line);
      }
    }
    if (setUpstream && sourceName.length > 0 &&
        destinationRef.startsWith('refs/heads/')) {
      lines.push(
        `branch '${snapshot.branch}' set up to track ` +
        `'${remoteName}/${displayDestination}'.`
      );
    }
    this.lastCommandExitCode = 0;
    return lines.join('\n');
  }

  private async nativeNetworkCommand(
    subcommand: string,
    args: string[],
    credentials?: RepositoryCredentials
  ): Promise<string> {
    if (subcommand === 'ls-remote') {
      return this.nativeListRemote(args, credentials);
    }
    if (subcommand === 'clone') {
      return this.nativeClone(args, credentials);
    }
    if (subcommand === 'pull') {
      return this.nativePull(args, credentials);
    }
    if (subcommand === 'push') {
      return this.nativePush(args, credentials);
    }
    return this.nativeFetch(args, credentials);
  }

  private async nativeClone(
    args: string[],
    credentials?: RepositoryCredentials
  ): Promise<string> {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    let remoteName: string = 'origin';
    let noCheckout: boolean = false;
    const values: string[] = [];
    for (let index = 0; index < args.length; index++) {
      const value: string = args[index];
      if (value === '--no-checkout' || value === '-n') {
        noCheckout = true;
      } else if (value === '--origin' || value === '-o') {
        if (index + 1 >= args.length) {
          this.lastCommandExitCode = 129;
          return `error: option '${value}' requires a value`;
        }
        remoteName = args[++index];
      } else if (value.startsWith('--origin=')) {
        remoteName = value.slice('--origin='.length);
      } else if (value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      } else {
        values.push(value);
      }
    }
    if (values.length < 1 || values.length > 2) {
      this.lastCommandExitCode = 129;
      return 'usage: git clone [--origin <name>] [--no-checkout] <repository> [<directory>]';
    }

    const url: string = values[0];
    const advertisement: RepositoryRemoteAdvertisement =
      this.repositoryService.listRemoteReferences(
        url,
        true,
        false,
        true,
        [],
        credentials
      );
    this.recordNetworkResponse(advertisement.responseCode, url);
    if (!advertisement.success) {
      this.lastCommandExitCode = 128;
      return `fatal: unable to access '${this.redactRemoteUrl(url)}': ${advertisement.error}`;
    }
    const directoryName: string =
      values[1] ?? this.defaultCloneDirectory(url);
    const targetPath: string = this.resolvePath(directoryName);
    if (this.repositoryService.directoryExists(targetPath)) {
      let entries: string[] = [];
      try {
        entries = this.repositoryService.listDirectory(targetPath);
      } catch (error) {
        this.lastCommandExitCode = 128;
        return `fatal: ${this.errorMessage(error)}`;
      }
      if (entries.length > 0) {
        this.lastCommandExitCode = 128;
        return `fatal: destination path '${directoryName}' already exists and is not an empty directory.`;
      }
    }

    const initialized: RepositorySnapshot =
      this.repositoryService.initialize(targetPath, false);
    if (!initialized.valid) {
      this.lastCommandExitCode = 128;
      return `fatal: ${initialized.error}`;
    }
    const added: RepositoryOperation =
      this.repositoryService.addRemote(targetPath, remoteName, url);
    if (!added.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${added.error}`;
    }
    const fetched: RepositoryOperation =
      await this.repositoryService.fetch(
        targetPath,
        remoteName,
        credentials
      );
    this.recordNetworkResponse(fetched.responseCode, url);
    if (!fetched.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${fetched.error}`;
    }
    const branch: string =
      this.remoteHeadBranch(targetPath, remoteName);
    if (branch.length === 0) {
      this.lastCommandExitCode = 128;
      return 'fatal: remote did not provide a usable default branch.';
    }
    const remoteReference: string =
      `refs/remotes/${remoteName}/${branch}`;
    if (noCheckout) {
      const remoteObjectId: string =
        this.referenceObjectId(targetPath, remoteReference);
      if (remoteObjectId.length === 0) {
        this.lastCommandExitCode = 128;
        return `fatal: couldn't find remote ref ${branch}`;
      }
      const localReference: string = `refs/heads/${branch}`;
      const created: RepositoryOperation =
        this.repositoryService.updateRef(
          targetPath,
          localReference,
          remoteObjectId,
          '',
          false,
          false,
          `clone: from ${this.redactRemoteUrl(url)}`,
          true
        );
      if (!created.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${created.error}`;
      }
      const head: RepositoryOperation =
        this.repositoryService.updateSymbolicRef(
          targetPath,
          'HEAD',
          localReference,
          false,
          `clone: from ${this.redactRemoteUrl(url)}`
        );
      if (!head.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${head.error}`;
      }
    } else {
      const checkout: RepositoryOperation =
        this.repositoryService.checkoutBranch(
          targetPath,
          branch,
          remoteReference
        );
      if (!checkout.success) {
        this.lastCommandExitCode = 128;
        return `fatal: ${checkout.error}`;
      }
    }
    const remoteConfig: RepositoryOperation =
      this.repositoryService.setConfig(
        targetPath,
        `branch.${branch}.remote`,
        remoteName,
        'local',
        false
      );
    if (!remoteConfig.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${remoteConfig.error}`;
    }
    const mergeConfig: RepositoryOperation =
      this.repositoryService.setConfig(
        targetPath,
        `branch.${branch}.merge`,
        `refs/heads/${branch}`,
        'local',
        false
      );
    if (!mergeConfig.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${mergeConfig.error}`;
    }
    this.lastCommandExitCode = 0;
    return this.cloneOutput(directoryName, fetched.output);
  }

  private async nativePull(
    args: string[],
    credentials?: RepositoryCredentials
  ): Promise<string> {
    if (this.repositoryService === undefined) {
      this.lastCommandExitCode = 128;
      return 'fatal: native Git repository service is unavailable.';
    }
    let quiet: boolean = false;
    const values: string[] = [];
    for (const value of args) {
      if (value === '--quiet' || value === '-q') {
        quiet = true;
      } else if (value === '--verbose' || value === '-v') {
        continue;
      } else if (value.startsWith('-')) {
        this.lastCommandExitCode = 129;
        return `error: unknown option '${value}'`;
      } else {
        values.push(value);
      }
    }
    if (values.length > 2) {
      this.lastCommandExitCode = 129;
      return 'usage: git pull [<remote>] [<branch>]';
    }
    const snapshot: RepositorySnapshot | undefined =
      this.currentRepository();
    if (snapshot === undefined || !snapshot.valid) {
      this.lastCommandExitCode = 128;
      return 'fatal: not a git repository (or any of the parent directories): .git';
    }
    if (snapshot.detached || snapshot.branch.length === 0) {
      this.lastCommandExitCode = 128;
      return 'fatal: cannot pull while HEAD is detached.';
    }
    const config: RepositoryConfigEntry[] =
      this.repositoryService.readConfig(this.currentPath, 'all', true);
    const configuredRemote: string =
      this.configValue(
        config,
        `branch.${snapshot.branch}.remote`
      );
    const configuredMerge: string =
      this.configValue(
        config,
        `branch.${snapshot.branch}.merge`
      );
    const remoteName: string =
      values[0] ?? (configuredRemote.length > 0 ? configuredRemote : 'origin');
    const configuredBranch: string =
      configuredMerge.startsWith('refs/heads/')
        ? configuredMerge.slice('refs/heads/'.length)
        : configuredMerge;
    const branch: string =
      values[1] ?? (configuredBranch.length > 0
        ? configuredBranch
        : snapshot.branch);
    const remoteUrl: string =
      this.networkRemoteUrl(remoteName, false);
    const fetched: RepositoryOperation =
      await this.repositoryService.fetch(
        this.currentPath,
        remoteName,
        credentials
      );
    this.recordNetworkResponse(fetched.responseCode, remoteUrl);
    if (!fetched.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${fetched.error}`;
    }
    const remoteReference: string =
      `refs/remotes/${remoteName}/${branch}`;
    const remoteObjectId: string =
      this.referenceObjectId(this.currentPath, remoteReference);
    if (remoteObjectId.length === 0) {
      this.lastCommandExitCode = 128;
      return `fatal: couldn't find remote ref ${branch}`;
    }
    if (snapshot.head === remoteObjectId) {
      this.lastCommandExitCode = 0;
      return quiet
        ? ''
        : this.networkOutput(fetched.output, 'Already up to date.');
    }
    if (snapshot.head.length > 0 &&
        !this.repositoryService.isAncestor(
          this.currentPath,
          snapshot.head,
          remoteObjectId
        )) {
      this.lastCommandExitCode = 128;
      return 'fatal: Not possible to fast-forward, aborting.';
    }
    const fastForward: RepositoryOperation =
      this.repositoryService.checkoutBranch(
        this.currentPath,
        snapshot.branch,
        remoteReference
      );
    if (!fastForward.success) {
      this.lastCommandExitCode = 128;
      return `fatal: ${fastForward.error}`;
    }
    this.lastCommandExitCode = 0;
    if (quiet) {
      return '';
    }
    return this.networkOutput(
      fetched.output,
      `Updating ${this.shortObjectId(snapshot.head)}..` +
      `${this.shortObjectId(remoteObjectId)}\nFast-forward`
    );
  }

  private networkRemoteUrl(
    remoteName: string,
    push: boolean
  ): string {
    if (this.repositoryService === undefined ||
        this.isRemoteUrl(remoteName)) {
      return remoteName;
    }
    try {
      return this.repositoryService.getRemoteUrl(
        this.currentPath,
        remoteName,
        push
      );
    } catch (_error) {
      return remoteName;
    }
  }

  private recordNetworkResponse(
    responseCode: number | undefined,
    remoteUrl: string
  ): void {
    this.lastNetworkResponseCode = responseCode ?? 0;
    this.lastNetworkRemoteUrl = remoteUrl;
  }

  private cloneOutput(
    directoryName: string,
    output: string[]
  ): string {
    const lines: string[] = [`Cloning into '${directoryName}'...`];
    lines.push(...output);
    return lines.join('\n');
  }

  private defaultCloneDirectory(url: string): string {
    let value: string = url.split(/[?#]/)[0];
    while (value.endsWith('/')) {
      value = value.slice(0, value.length - 1);
    }
    const separator: number = Math.max(
      value.lastIndexOf('/'),
      value.lastIndexOf(':')
    );
    let name: string = separator >= 0
      ? value.slice(separator + 1)
      : value;
    if (name.endsWith('.git')) {
      name = name.slice(0, name.length - 4);
    }
    return name.length > 0 ? name : 'repository';
  }

  private remoteHeadBranch(
    path: string,
    remoteName: string
  ): string {
    const remotePrefix: string = `refs/remotes/${remoteName}/`;
    try {
      const symbolic: string =
        this.repositoryService?.symbolicRef(
          path,
          `${remotePrefix}HEAD`,
          false,
          true
        ) ?? '';
      if (symbolic.startsWith(remotePrefix)) {
        return symbolic.slice(remotePrefix.length);
      }
    } catch (_error) {
      // Fall through to the common branch names below.
    }
    const references: string[] =
      this.repositoryService?.showRef(path, {
        heads: false,
        tags: false,
        includeHead: false,
        dereference: false,
        verify: false,
        quiet: false,
        hashOnly: false,
        abbreviation: 40,
        patterns: []
      }) ?? [];
    const branches: string[] = references.map(
      (line: string): string => {
        const parts: string[] = this.referenceLineParts(line);
        const reference: string = parts[1] ?? '';
        return reference.startsWith(remotePrefix)
          ? reference.slice(remotePrefix.length)
          : '';
      }
    ).filter(
      (branch: string): boolean =>
        branch.length > 0 && branch !== 'HEAD'
    );
    if (branches.includes('main')) {
      return 'main';
    }
    if (branches.includes('master')) {
      return 'master';
    }
    return branches[0] ?? '';
  }

  private referenceObjectId(
    path: string,
    referenceName: string
  ): string {
    const references: string[] =
      this.repositoryService?.showRef(path, {
        heads: false,
        tags: false,
        includeHead: false,
        dereference: false,
        verify: false,
        quiet: false,
        hashOnly: false,
        abbreviation: 40,
        patterns: [referenceName]
      }) ?? [];
    for (const line of references) {
      const parts: string[] = this.referenceLineParts(line);
      if (parts.length === 2 && parts[1] === referenceName) {
        return parts[0];
      }
    }
    return '';
  }

  private parseGitInvocation(args: string[]): GitInvocation {
    const config: RepositoryConfigEntry[] = [];
    let index: number = 0;
    while (index < args.length) {
      const option: string = args[index];
      let assignment: string = '';
      if (option === '-c') {
        if (index + 1 >= args.length) {
          return {
            args: [],
            config: [],
            error: 'error: switch `c\' requires a value'
          };
        }
        assignment = args[index + 1];
        index += 2;
      } else if (option.startsWith('-c') && option.length > 2) {
        assignment = option.slice(2);
        index++;
      } else {
        break;
      }
      const separator: number = assignment.indexOf('=');
      const key: string =
        separator < 0 ? assignment : assignment.slice(0, separator);
      if (this.normalizeConfigKey(key).length === 0) {
        return {
          args: [],
          config: [],
          error: `error: invalid config key '${key}'`
        };
      }
      config.push({
        key,
        value: separator < 0
          ? 'true'
          : assignment.slice(separator + 1)
      });
    }
    return {
      args: args.slice(index),
      config,
      error: ''
    };
  }

  private normalizeConfigKey(value: string): string {
    const trimmed: string = value.trim();
    const firstSeparator: number = trimmed.indexOf('.');
    const lastSeparator: number = trimmed.lastIndexOf('.');
    if (firstSeparator <= 0 ||
        lastSeparator <= 0 ||
        lastSeparator + 1 >= trimmed.length) {
      return '';
    }
    const section: string =
      trimmed.slice(0, firstSeparator).toLowerCase();
    const subsection: string =
      firstSeparator === lastSeparator
        ? ''
        : trimmed.slice(firstSeparator + 1, lastSeparator);
    const key: string =
      trimmed.slice(lastSeparator + 1).toLowerCase();
    return `${section}.${subsection.length > 0
      ? `${subsection}.`
      : ''}${key}`;
  }

  private validConfigType(value: string): boolean {
    return [
      'bool',
      'int',
      'bool-or-int',
      'bool-or-str',
      'path',
      'expiry-date'
    ].includes(value);
  }

  private normalizeConfigValue(
    value: string,
    type: string,
    writing: boolean
  ): string {
    if (type.length === 0 || (writing && type === 'path')) {
      return value;
    }
    if (type === 'bool') {
      const booleanValue: string | undefined =
        this.configBooleanValue(value);
      if (booleanValue === undefined) {
        throw new Error(`bad boolean config value '${value}'`);
      }
      return booleanValue;
    }
    if (type === 'int') {
      return this.configIntegerValue(value);
    }
    if (type === 'bool-or-int') {
      if (/^[+-]?\d+[kKmMgG]?$/.test(value.trim())) {
        return this.configIntegerValue(value);
      }
      const booleanValue: string | undefined =
        this.configBooleanValue(value);
      if (booleanValue === undefined) {
        throw new Error(`bad numeric config value '${value}'`);
      }
      return booleanValue;
    }
    if (type === 'bool-or-str') {
      const booleanValue: string | undefined =
        this.configBooleanValue(value);
      return booleanValue === undefined ? value : booleanValue;
    }
    if (type === 'path') {
      if (value === '~') {
        return this.homePath;
      }
      if (value.startsWith('~/')) {
        return this.normalizePath(
          `${this.homePath}/${value.slice(2)}`
        );
      }
      return value;
    }
    if (type === 'expiry-date') {
      if (writing) {
        return value;
      }
      const normalized: string = value.trim().toLowerCase();
      if (normalized === 'never') {
        return '0';
      }
      if (/^\d+$/.test(normalized)) {
        return normalized;
      }
      if (normalized === 'now') {
        return String(Math.floor(Date.now() / 1000));
      }
      const milliseconds: number = Date.parse(value);
      if (Number.isFinite(milliseconds)) {
        return String(Math.floor(milliseconds / 1000));
      }
      throw new Error(`invalid expiry date '${value}'`);
    }
    throw new Error(`invalid type specifier '${type}'`);
  }

  private configBooleanValue(value: string): string | undefined {
    const normalized: string = value.trim().toLowerCase();
    if (normalized.length === 0 ||
        normalized === 'true' ||
        normalized === 'yes' ||
        normalized === 'on' ||
        normalized === '1') {
      return 'true';
    }
    if (normalized === 'false' ||
        normalized === 'no' ||
        normalized === 'off' ||
        normalized === '0') {
      return 'false';
    }
    return undefined;
  }

  private configIntegerValue(value: string): string {
    let normalized: string = value.trim();
    let multiplier: number = 1;
    if (normalized.length > 0) {
      const suffix: string =
        normalized[normalized.length - 1].toLowerCase();
      if (suffix === 'k' || suffix === 'm' || suffix === 'g') {
        normalized = normalized.slice(0, normalized.length - 1);
        multiplier = suffix === 'k'
          ? 1024
          : suffix === 'm'
            ? 1024 * 1024
            : 1024 * 1024 * 1024;
      }
    }
    if (!/^[+-]?\d+$/.test(normalized)) {
      throw new Error(`bad numeric config value '${value}'`);
    }
    const parsed: number = Number(normalized) * multiplier;
    if (!Number.isFinite(parsed) || !Number.isInteger(parsed)) {
      throw new Error(`bad numeric config value '${value}'`);
    }
    return String(parsed);
  }

  private configValue(
    entries: RepositoryConfigEntry[],
    key: string
  ): string {
    for (let index: number = entries.length - 1; index >= 0; index--) {
      if (entries[index].key === key) {
        return entries[index].value;
      }
    }
    return '';
  }

  private shortObjectId(value: string): string {
    return value.length > 7 ? value.slice(0, 7) : value;
  }

  private referenceLineParts(line: string): string[] {
    const space: number = line.indexOf(' ');
    const tab: number = line.indexOf('\t');
    let separator: number = -1;
    if (space >= 0 && tab >= 0) {
      separator = Math.min(space, tab);
    } else if (space >= 0) {
      separator = space;
    } else {
      separator = tab;
    }
    if (separator < 0) {
      return [];
    }
    return [
      line.slice(0, separator),
      line.slice(separator + 1).trim()
    ];
  }

  private networkOutput(output: string[], summary: string): string {
    const lines: string[] = output.slice();
    if (summary.length > 0) {
      lines.push(summary);
    }
    return lines.join('\n');
  }

  private isRemoteUrl(value: string): boolean {
    return value.includes('://') ||
      (value.includes('@') && value.includes(':'));
  }

  private redactRemoteUrl(value: string): string {
    const schemeEnd: number = value.indexOf('://');
    if (schemeEnd < 0) {
      return value;
    }
    const authorityStart: number = schemeEnd + 3;
    const pathStart: number = value.indexOf('/', authorityStart);
    const authorityEnd: number =
      pathStart < 0 ? value.length : pathStart;
    const at: number = value.lastIndexOf('@', authorityEnd);
    return at >= authorityStart
      ? `${value.slice(0, authorityStart)}${value.slice(at + 1)}`
      : value;
  }

  private nativeRemote(
    snapshot: RepositorySnapshot,
    args: string[]
  ): string {
    if (args.length === 0) {
      return snapshot.remotes.map(
        (remote: RepositoryRemote): string => remote.name
      ).join('\n');
    }
    if (args[0] === '-v') {
      const lines: string[] = [];
      for (const remote of snapshot.remotes) {
        lines.push(`${remote.name}\t${remote.fetchUrl} (fetch)`);
        lines.push(`${remote.name}\t${remote.pushUrl} (push)`);
      }
      return lines.join('\n');
    }
    if (args[0] === 'add') {
      if (args.length < 3) {
        return 'error: remote add requires a name and URL';
      }
      const operation: RepositoryOperation =
        this.repositoryService?.addRemote(
          this.currentPath,
          args[1],
          args[2]
        ) as RepositoryOperation;
      return operation.success ? '' : `error: ${operation.error}`;
    }
    if (args[0] === 'remove' || args[0] === 'rm') {
      const name: string = args[1] ?? '';
      if (name.length === 0) {
        return 'error: remote remove requires a name';
      }
      const operation: RepositoryOperation =
        this.repositoryService?.removeRemote(
          this.currentPath,
          name
        ) as RepositoryOperation;
      return operation.success ? '' : `error: ${operation.error}`;
    }
    if (args[0] === 'rename') {
      if (args.length < 3) {
        return 'error: remote rename requires an old and new name';
      }
      const operation: RepositoryOperation =
        this.repositoryService?.renameRemote(
          this.currentPath,
          args[1],
          args[2]
        ) as RepositoryOperation;
      return operation.success ? '' : `error: ${operation.error}`;
    }
    if (args[0] === 'get-url') {
      const push: boolean = args.includes('--push');
      const name: string =
        args.slice(1).find((value: string): boolean => !value.startsWith('-')) ?? '';
      if (name.length === 0) {
        return 'error: remote get-url requires a name';
      }
      try {
        return this.repositoryService?.getRemoteUrl(
          this.currentPath,
          name,
          push
        ) ?? '';
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }
    if (args[0] === 'set-url') {
      const push: boolean = args.includes('--push');
      const values: string[] = args.slice(1).filter(
        (value: string): boolean => !value.startsWith('-')
      );
      if (values.length < 2) {
        return 'error: remote set-url requires a name and URL';
      }
      const operation: RepositoryOperation =
        this.repositoryService?.setRemoteUrl(
          this.currentPath,
          values[0],
          values[1],
          push
        ) as RepositoryOperation;
      return operation.success ? '' : `error: ${operation.error}`;
    }
    return `error: unsupported native remote command '${args.join(' ')}'`;
  }

  private nativeRevParse(
    snapshot: RepositorySnapshot | undefined,
    args: string[]
  ): string {
    if (snapshot === undefined || !snapshot.valid) {
      return 'fatal: not a git repository';
    }
    if (args.includes('--show-toplevel')) {
      return snapshot.repositoryPath;
    }
    if (args.includes('--git-dir')) {
      return snapshot.gitDirectory;
    }
    if (args.includes('--abbrev-ref') && args.includes('HEAD')) {
      return snapshot.detached ? 'HEAD' : snapshot.branch;
    }
    if (args.includes('HEAD')) {
      return snapshot.head.length > 0
        ? snapshot.head
        : 'fatal: ambiguous argument \'HEAD\': unknown revision';
    }
    return 'usage: git rev-parse [--show-toplevel | --git-dir | --abbrev-ref HEAD | HEAD]';
  }

  private nativeFileState(file: RepositoryFileStatus): string {
    if (file.workTreeState === 'D' || file.indexState === 'D') {
      return 'deleted';
    }
    if (file.indexState === 'A') {
      return 'added';
    }
    return 'modified';
  }

  private nativeStateLabel(state: string): string {
    if (state === 'A') {
      return 'new file';
    }
    if (state === 'D') {
      return 'deleted';
    }
    if (state === 'U') {
      return 'unmerged';
    }
    return 'modified';
  }

  private resolvePath(destination: string): string {
    if (destination === '~') {
      return this.homePath;
    }
    if (destination.startsWith('~/')) {
      return this.normalizePath(
        `${this.homePath}/${destination.slice(2)}`
      );
    }
    if (destination.startsWith('/')) {
      return this.normalizePath(destination);
    }
    return this.normalizePath(`${this.currentPath}/${destination}`);
  }

  private normalizePath(path: string): string {
    const segments: string[] = [];
    for (const segment of path.split('/')) {
      if (segment.length === 0 || segment === '.') {
        continue;
      }
      if (segment === '..') {
        segments.pop();
      } else {
        segments.push(segment);
      }
    }
    return `/${segments.join('/')}`;
  }

  private errorMessage(error: Object): string {
    return error instanceof Error ? error.message : `${error}`;
  }
}
