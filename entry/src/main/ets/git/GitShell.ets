import {
  GitRepositoryService,
  RepositoryConfigEntry,
  RepositoryFileStatus,
  RepositoryRemote,
  RepositoryReflogEntry,
  RepositoryOperation,
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

export class GitShell {
  private currentPath: string = '/home/harmony/workspace/demo-app';
  private homePath: string = '/home/harmony';
  private branch: string = 'main';
  private repositoryService?: GitRepositoryService;
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

  attachRepositoryService(
    repositoryService: GitRepositoryService,
    homePath: string
  ): string {
    this.repositoryService = repositoryService;
    this.homePath = homePath;
    const demoPath: string = `${homePath}/workspace/demo-app`;
    const snapshot: RepositorySnapshot =
      repositoryService.initialize(demoPath, true);
    if (!snapshot.valid) {
      this.currentPath = homePath;
      return `fatal: native Git service initialization failed: ${snapshot.error}`;
    }
    this.currentPath = snapshot.repositoryPath;
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
    this.currentPath = snapshot.repositoryPath;
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

  execute(rawCommand: string): string {
    const command: string = rawCommand.trim();
    if (command.length === 0) {
      return '';
    }
    const tokens: string[] = this.tokenize(command);
    const verb: string = tokens[0];
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
          return `bash: ${verb}: ${this.errorMessage(error)}`;
        }
      }
      return 'README.md  docs  src  .git';
    }
    if (verb === 'cd') {
      return this.changeDirectory(tokens.slice(1));
    }
    if (verb !== 'git') {
      return `bash: ${verb}: command not found\nType 'help' to see the available Harmony Git Bash commands.`;
    }
    return this.executeGit(tokens.slice(1));
  }

  private executeGit(args: string[]): string {
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
      if (subcommand === 'fetch' || subcommand === 'pull' ||
          subcommand === 'push' || subcommand === 'clone') {
        return `git ${subcommand}: network transport will be handled by the Harmony native Git service.\nThe terminal contract is ready; libgit2/Git transport binding is the next implementation milestone.`;
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
      if (subcommand === 'tag') {
        return this.nativeTag(args.slice(1));
      }
      if (subcommand === 'config') {
        return this.nativeConfig(args.slice(1));
      }
      if (subcommand === 'reflog') {
        return this.nativeReflog(args.slice(1));
      }
      return `git: '${subcommand}' is not a supported Harmony Git Bash command yet.\nSupported native commands: open, status, add, rm, mv, restore, reset, commit, diff, log, show, tag, reflog, config, branch, switch, checkout, remote, rev-parse, init.`;
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
    return `git: '${subcommand}' is not a supported Harmony Git Bash command yet.\nSupported: status, add, restore, reset, commit, log, branch, switch, checkout, diff, remote, init.`;
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
    const destination: string = args[0] ?? '~';
    if (this.repositoryService !== undefined) {
      const candidate: string = this.resolvePath(destination);
      if (!this.repositoryService.directoryExists(candidate)) {
        return `bash: cd: ${destination}: No such file or directory`;
      }
      this.currentPath = candidate;
      return '';
    }
    if (destination === '~') {
      this.currentPath = '/home/harmony';
      return '';
    }
    if (destination === '..') {
      const index: number = this.currentPath.lastIndexOf('/');
      this.currentPath = index > 0 ? this.currentPath.slice(0, index) : '/';
      return '';
    }
    if (destination === '.' || destination === 'demo-app') {
      return '';
    }
    if (destination === 'workspace') {
      this.currentPath = '/home/harmony/workspace';
      return '';
    }
    return `bash: cd: ${destination}: No such file or directory`;
  }

  private tokenize(input: string): string[] {
    const tokens: string[] = [];
    let current: string = '';
    let quote: string = '';
    for (let index: number = 0; index < input.length; index++) {
      const character: string = input[index];
      if ((character === '"' || character === "'") && (quote.length === 0 || quote === character)) {
        quote = quote.length === 0 ? character : '';
      } else if (character === ' ' && quote.length === 0) {
        if (current.length > 0) {
          tokens.push(current);
          current = '';
        }
      } else {
        current += character;
      }
    }
    if (current.length > 0) {
      tokens.push(current);
    }
    return tokens;
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
    return 'Harmony Git Bash compatibility session\n\nShell: pwd, ls, cd, clear, help\nGit: open, status, rev-parse, add, rm, mv, restore, reset --hard, commit -m, log, show, tag, reflog, config, branch, switch, checkout, diff, remote, init\n\nLocal repository reads and writes use the Harmony native Git service. Network transport remains unavailable until the native transport layer is enabled.';
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

  private nativeLog(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
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
    for (const value of args) {
      if (value === '--stat') {
        statOnly = true;
      } else if (value === '--oneline') {
        oneLine = true;
      } else if (value === '--') {
        return 'error: path-limited git show is not supported yet';
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
        oneLine
      );
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
  }

  private nativeTag(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let annotated: boolean = false;
    let deleteMode: boolean = false;
    let force: boolean = false;
    let listMode: boolean = false;
    let message: string = '';
    let parseOptions: boolean = true;
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
        message = args[index + 1];
        annotated = true;
        index++;
      } else if (parseOptions && value.startsWith('--message=')) {
        message = value.slice('--message='.length);
        annotated = true;
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
      if (values.length > 0) {
        return 'error: tag list patterns are not supported yet';
      }
      try {
        return this.repositoryService.tags(this.currentPath).join('\n');
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    if (values.length === 0 || values.length > 2) {
      return 'usage: git tag [-a] [-f] [-m <message>] <tagname> [<commit>]';
    }
    if (annotated && message.trim().length === 0) {
      return 'fatal: annotated tags require -m because editor integration is not available yet';
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

  private nativeConfig(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    const localArgs: string[] = args.filter(
      (value: string): boolean => value !== '--local'
    );
    if (localArgs.includes('--list') || localArgs.includes('-l')) {
      try {
        return this.repositoryService.readConfig(this.currentPath).map(
          (entry: RepositoryConfigEntry): string =>
            `${entry.key}\t${entry.value}`
        ).join('\n');
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    const modeIndex: number = localArgs.findIndex(
      (value: string): boolean =>
        value === '--get' || value === '--unset'
    );
    if (modeIndex >= 0) {
      const mode: string = localArgs[modeIndex];
      const key: string = localArgs[modeIndex + 1] ?? '';
      if (key.length === 0 || key.startsWith('-')) {
        return `error: ${mode} requires a key`;
      }
      if (mode === '--get') {
        try {
          return this.repositoryService.readConfig(this.currentPath).filter(
            (entry: RepositoryConfigEntry): boolean =>
              entry.key === key.toLowerCase()
          ).map(
            (entry: RepositoryConfigEntry): string => entry.value
          ).join('\n');
        } catch (error) {
          return `fatal: ${this.errorMessage(error)}`;
        }
      }
      const operation: RepositoryOperation =
        this.repositoryService.unsetConfig(this.currentPath, key);
      if (!operation.success) {
        return `error: ${operation.error}`;
      }
      return '';
    }

    if (localArgs.length === 1) {
      try {
        return this.repositoryService.readConfig(this.currentPath).filter(
          (entry: RepositoryConfigEntry): boolean =>
            entry.key === localArgs[0].toLowerCase()
        ).map(
          (entry: RepositoryConfigEntry): string => entry.value
        ).join('\n');
      } catch (error) {
        return `fatal: ${this.errorMessage(error)}`;
      }
    }

    if (localArgs.length === 2) {
      const operation: RepositoryOperation =
        this.repositoryService.setConfig(
          this.currentPath,
          localArgs[0],
          localArgs[1]
        );
      if (!operation.success) {
        return `error: ${operation.error}`;
      }
      return '';
    }
    return 'usage: git config [--local] [--list | --get key | --unset key | key [value]]';
  }

  private nativeReflog(args: string[]): string {
    if (this.repositoryService === undefined) {
      return 'fatal: native Git repository service is unavailable.';
    }
    let ref: string = '';
    let maxCount: number = 100;
    for (let index: number = 0; index < args.length; index++) {
      const value: string = args[index];
      if (value === '-n' || value === '--max-count') {
        maxCount = Number(args[index + 1] ?? 100);
        index++;
      } else if (value.startsWith('--max-count=')) {
        maxCount = Number(value.slice('--max-count='.length));
      } else if (!value.startsWith('-') && ref.length === 0) {
        ref = value;
      }
    }
    if (!Number.isFinite(maxCount) || maxCount < 0) {
      return 'error: invalid max-count value';
    }
    try {
      const entries: RepositoryReflogEntry[] =
        this.repositoryService.reflog(this.currentPath, ref, maxCount);
      const displayRef: string = ref.length === 0 ? 'HEAD' : ref;
      return entries.map(
        (entry: RepositoryReflogEntry, index: number): string =>
          `${entry.newId.slice(0, 7)} ${displayRef}@{${index}}: ${entry.message}`
      ).join('\n');
    } catch (error) {
      return `fatal: ${this.errorMessage(error)}`;
    }
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
