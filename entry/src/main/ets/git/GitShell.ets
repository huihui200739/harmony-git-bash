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
  private branch: string = 'main';
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

  prompt(): string {
    const path: string = this.currentPath.replace('/home/harmony', '~');
    return `harmony@PC MINGW64 ${path} (${this.branch})`;
  }

  snapshot(): GitShellSnapshot {
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
    if (subcommand === 'help') {
      return this.helpText();
    }
    if (subcommand === 'status') {
      return this.status(args.includes('--short') || args.includes('-s'));
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
    if (subcommand === 'fetch' || subcommand === 'pull' || subcommand === 'push' || subcommand === 'clone') {
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
    return 'Harmony Git Bash compatibility session\n\nShell: pwd, ls, cd, clear, help\nGit: status, add, restore --staged, reset, commit -m, log, branch, switch, checkout, diff, remote, init\n\nNetwork commands (clone, fetch, pull, push) are reserved for the upcoming Harmony native Git service.';
  }
}
