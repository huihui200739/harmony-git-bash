export interface TerminalCompletion {
  value: string;
  candidates: string[];
}

const SHELL_COMMANDS: string[] = [
  'cat',
  'cd',
  'clear',
  'dir',
  'echo',
  'env',
  'export',
  'git',
  'help',
  'ls',
  'printenv',
  'printf',
  'pwd',
  'set',
  'unset'
];

const GIT_COMMANDS: string[] = [
  'add',
  'branch',
  'cat-file',
  'check-ignore',
  'checkout',
  'clean',
  'clone',
  'commit',
  'config',
  'diff',
  'fetch',
  'for-each-ref',
  'hash-object',
  'help',
  'init',
  'log',
  'ls-files',
  'ls-remote',
  'ls-tree',
  'merge-base',
  'mv',
  'open',
  'pull',
  'push',
  'reflog',
  'remote',
  'reset',
  'restore',
  'rev-list',
  'rev-parse',
  'rm',
  'show',
  'show-ref',
  'status',
  'switch',
  'symbolic-ref',
  'tag',
  'update-ref'
];

const GIT_REMOTE_COMMANDS: string[] = [
  'add',
  'get-url',
  'remove',
  'rename',
  'set-url'
];

const GIT_REFLOG_COMMANDS: string[] = [
  'delete',
  'drop',
  'exists',
  'expire',
  'list',
  'show',
  'write'
];

export class TerminalInputSession {
  private readonly history: string[] = [];
  private historyIndex: number = 0;
  private draft: string = '';
  private readonly historyLimit: number;

  constructor(historyLimit: number = 200) {
    this.historyLimit = Math.max(1, historyLimit);
  }

  record(rawCommand: string): void {
    const command: string = rawCommand.trim();
    if (command.length === 0) {
      this.resetNavigation('');
      return;
    }
    if (this.history[this.history.length - 1] !== command) {
      this.history.push(command);
      while (this.history.length > this.historyLimit) {
        this.history.shift();
      }
    }
    this.resetNavigation('');
  }

  edit(value: string): void {
    if (this.historyIndex < this.history.length &&
        this.history[this.historyIndex] === value) {
      return;
    }
    this.resetNavigation(value);
  }

  previous(currentValue: string): string {
    if (this.history.length === 0) {
      return currentValue;
    }
    if (this.historyIndex >= this.history.length) {
      this.draft = currentValue;
      this.historyIndex = this.history.length;
    }
    if (this.historyIndex > 0) {
      this.historyIndex--;
    }
    return this.history[this.historyIndex];
  }

  next(): string {
    if (this.history.length === 0) {
      return this.draft;
    }
    if (this.historyIndex < this.history.length - 1) {
      this.historyIndex++;
      return this.history[this.historyIndex];
    }
    this.historyIndex = this.history.length;
    return this.draft;
  }

  complete(
    input: string,
    pathCandidates: string[] = []
  ): TerminalCompletion {
    const tokenStart: number = this.activeTokenStart(input);
    const fragment: string = input.slice(tokenStart);
    const commandPrefix: string = input.slice(0, tokenStart);
    const candidates: string[] =
      this.candidatesFor(commandPrefix, pathCandidates)
        .filter((candidate: string): boolean =>
          candidate.startsWith(fragment))
        .filter((candidate: string, index: number, values: string[]): boolean =>
          values.indexOf(candidate) === index)
        .sort();
    if (candidates.length === 0) {
      return { value: input, candidates: [] };
    }
    if (candidates.length === 1) {
      const candidate: string = candidates[0];
      const suffix: string = candidate.endsWith('/') ? '' : ' ';
      return {
        value: `${input.slice(0, tokenStart)}${candidate}${suffix}`,
        candidates
      };
    }
    const commonPrefix: string = this.longestCommonPrefix(candidates);
    return {
      value: `${input.slice(0, tokenStart)}${commonPrefix}`,
      candidates
    };
  }

  entries(): string[] {
    return this.history.slice();
  }

  private resetNavigation(draft: string): void {
    this.historyIndex = this.history.length;
    this.draft = draft;
  }

  private activeTokenStart(input: string): number {
    let quote: string = '';
    let escaped: boolean = false;
    let tokenStart: number = 0;
    for (let index: number = 0; index < input.length; index++) {
      const character: string = input[index];
      if (escaped) {
        escaped = false;
      } else if (character === '\\' && quote !== "'") {
        escaped = true;
      } else if ((character === '"' || character === "'") &&
          (quote.length === 0 || quote === character)) {
        quote = quote.length === 0 ? character : '';
      } else if ((character === ' ' || character === '\t' ||
          character === '|') && quote.length === 0) {
        tokenStart = index + 1;
      }
    }
    return tokenStart;
  }

  private candidatesFor(
    commandPrefix: string,
    pathCandidates: string[]
  ): string[] {
    const pipelineStart: number = commandPrefix.lastIndexOf('|') + 1;
    const tokens: string[] = commandPrefix.slice(pipelineStart)
      .trim()
      .split(/\s+/)
      .filter((value: string): boolean => value.length > 0);
    if (tokens.length === 0) {
      return SHELL_COMMANDS;
    }
    if (tokens.length === 1 && tokens[0] === 'git') {
      return GIT_COMMANDS;
    }
    if (tokens.length === 2 &&
        tokens[0] === 'git' &&
        tokens[1] === 'remote') {
      return GIT_REMOTE_COMMANDS;
    }
    if (tokens.length === 2 &&
        tokens[0] === 'git' &&
        tokens[1] === 'reflog') {
      return GIT_REFLOG_COMMANDS;
    }
    return pathCandidates;
  }

  private longestCommonPrefix(values: string[]): string {
    let prefix: string = values[0] ?? '';
    for (let index: number = 1; index < values.length; index++) {
      while (!values[index].startsWith(prefix) && prefix.length > 0) {
        prefix = prefix.slice(0, prefix.length - 1);
      }
    }
    return prefix;
  }
}
