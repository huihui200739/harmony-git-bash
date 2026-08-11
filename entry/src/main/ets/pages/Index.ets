import { picker } from '@kit.CoreFileKit';
import { KeyCode } from '@kit.InputKit';
import { GitShell } from '../git/GitShell';
import { HarmonyCredentialStore } from '../git/HarmonyCredentialStore';
import { NativeGitRepositoryService } from '../git/NativeGitRepositoryService';
import { redactCredentialText } from '../git/CredentialSecurity';
import {
  TerminalCompletion,
  TerminalInputSession
} from '../terminal/TerminalInputSession';
import {
  measureTerminalViewport,
  parseTerminalAnsi,
  TerminalAnsiSegment,
  TerminalAnsiText,
  TerminalViewport
} from '../terminal/TerminalAnsi';

interface TerminalLine {
  text: string;
  segments: TerminalAnsiSegment[];
  copyable: boolean;
}

@Entry
@Component
struct Index {
  @State private command: string = '';
  @State private lines: TerminalLine[] = [];
  private shell: GitShell = new GitShell(new HarmonyCredentialStore());
  private inputSession: TerminalInputSession =
    new TerminalInputSession();
  private inputController: TextInputController =
    new TextInputController();
  private controlKeyHeld: boolean = false;

  aboutToAppear(): void {
    const context = this.getUIContext().getHostContext();
    let nativeMessage: string = '';
    if (context !== undefined) {
      nativeMessage = this.shell.attachRepositoryService(
        new NativeGitRepositoryService(),
        context.filesDir
      );
    }
    this.lines = [
      this.terminalLine('Welcome to Harmony Git Bash', '#d8dee9', true),
      this.terminalLine(
        'Git for Windows compatibility shell for HarmonyOS PC',
        '#8fbcbb',
        true
      ),
      this.terminalLine(
        'Type "help" for supported commands.',
        '#aab2c0',
        true
      ),
      this.terminalLine(
        nativeMessage,
        this.outputTone(nativeMessage),
        true
      ),
      this.terminalLine('', '#d8dee9', true)
    ];
  }

  private async executeCommand(): Promise<void> {
    const editing: boolean = this.shell.editorActive();
    const credentialPrompt: boolean =
      this.shell.credentialPromptActive();
    const input: string =
      editing || credentialPrompt ? this.command : this.command.trim();
    if (!editing && !credentialPrompt && input.length === 0) {
      return;
    }
    if (!editing && !credentialPrompt) {
      this.inputSession.record(input);
    }
    const displayInput: string = credentialPrompt
      ? input
      : redactCredentialText(input);
    const nextLines: TerminalLine[] = this.lines.slice();
    nextLines.push(
      this.terminalLine(
        this.shell.shouldEchoInput()
          ? `${this.shell.inputPrompt()} ${displayInput}`
          : this.shell.inputPrompt(),
        '#f8f8f2',
        !credentialPrompt
      )
    );
    const output: string =
      !editing && !credentialPrompt && input === 'git open'
        ? await this.selectRepository()
        : await this.shell.executeAsync(input);
    if (output === '__CLEAR__') {
      this.lines = [];
      this.command = '';
      return;
    }
    if (output.length > 0) {
      nextLines.push(
        this.terminalLine(
          redactCredentialText(output),
          this.outputTone(output),
          true
        )
      );
    }
    nextLines.push(this.terminalLine('', '#d8dee9', true));
    this.lines = nextLines;
    this.command = '';
  }

  private handleInputKey(event: KeyEvent): boolean {
    if (event.keyCode === KeyCode.KEYCODE_CTRL_LEFT ||
        event.keyCode === KeyCode.KEYCODE_CTRL_RIGHT) {
      this.controlKeyHeld = event.type === KeyType.Down;
      return true;
    }
    if (event.type === KeyType.Down &&
        event.keyCode === KeyCode.KEYCODE_C &&
        this.controlKeyHeld &&
        this.shell.networkOperationActive()) {
      this.shell.cancelActiveNetworkOperation();
      return true;
    }
    if (event.type === KeyType.Down &&
        event.keyCode === KeyCode.KEYCODE_C &&
        this.controlKeyHeld &&
        this.shell.heredocActive()) {
      this.shell.cancelHeredoc();
      return true;
    }
    if (this.shell.editorActive() ||
        this.shell.credentialPromptActive()) {
      return false;
    }
    if (event.type !== KeyType.Down) {
      return false;
    }
    if (event.keyCode === KeyCode.KEYCODE_DPAD_UP) {
      this.replaceCommand(this.inputSession.previous(this.command));
      return true;
    }
    if (event.keyCode === KeyCode.KEYCODE_DPAD_DOWN) {
      this.replaceCommand(this.inputSession.next());
      return true;
    }
    if (event.keyCode === KeyCode.KEYCODE_TAB) {
      const previousCommand: string = this.command;
      const completion: TerminalCompletion =
        this.inputSession.complete(
          this.command,
          this.shell.completionPaths(this.command)
        );
      this.replaceCommand(completion.value);
      if (completion.candidates.length > 1 &&
          completion.value === previousCommand) {
        this.lines = this.lines.concat([
          this.terminalLine(
            completion.candidates.join('  '),
            '#8fbcbb',
            true
          )
        ]);
      }
      return true;
    }
    return false;
  }

  private replaceCommand(value: string): void {
    this.command = value;
    this.inputController.caretPosition(value.length);
  }

  private async selectRepository(): Promise<string> {
    const context = this.getUIContext().getHostContext();
    if (context === undefined) {
      return 'fatal: cannot access the current HarmonyOS window context.';
    }
    const documentPicker: picker.DocumentViewPicker =
      new picker.DocumentViewPicker(context);
    const options: picker.DocumentSelectOptions =
      new picker.DocumentSelectOptions();
    options.maxSelectNumber = 1;
    options.selectMode = picker.DocumentSelectMode.FOLDER;
    try {
      const selected: string[] = await documentPicker.select(options);
      if (selected.length === 0) {
        return 'Repository selection cancelled.';
      }
      return this.shell.openRepository(selected[0]);
    } catch (error) {
      return `fatal: repository selection failed: ${this.errorMessage(error)}`;
    }
  }

  private errorMessage(error: Object): string {
    return error instanceof Error ? error.message : `${error}`;
  }

  private terminalLine(
    text: string,
    defaultColor: string,
    copyable: boolean
  ): TerminalLine {
    const parsed: TerminalAnsiText =
      parseTerminalAnsi(text, defaultColor);
    return {
      text: parsed.text,
      segments: parsed.segments,
      copyable
    };
  }

  private handleTerminalAreaChange(
    _oldValue: Area,
    newValue: Area
  ): void {
    const width: number = typeof newValue.width === 'number'
      ? newValue.width
      : 0;
    const height: number = typeof newValue.height === 'number'
      ? newValue.height
      : 0;
    const viewport: TerminalViewport =
      measureTerminalViewport(width, height, 36);
    this.shell.setTerminalSize(viewport.columns, viewport.rows);
  }

  private outputTone(output: string): string {
    if (output.startsWith('fatal:') || output.startsWith('error:') || output.startsWith('bash:')) {
      return '#ff7b72';
    }
    if (output.startsWith('Switched') || output.startsWith('Staged') || output.startsWith('Unstaged') || output.startsWith('[')) {
      return '#a5d6ff';
    }
    return '#d8dee9';
  }

  build() {
    Column() {
      Row() {
        Text('>_').width(44).fontSize(14).fontWeight(FontWeight.Bold)
          .fontColor('#7ee787').fontFamily('monospace')
        Text(`MINGW64:${this.shell.displayPath()}`)
          .fontSize(14).fontColor('#e6edf3').fontFamily('monospace').layoutWeight(1)
        Text('Harmony Git Bash 0.1.0')
          .fontSize(12).fontColor('#8b949e')
      }
      .height(40).width('100%').padding({ left: 14, right: 16 })
      .backgroundColor('#21262d')

      Scroll() {
        Column({ space: 4 }) {
          ForEach(this.lines, (line: TerminalLine): void => {
            Text() {
              ForEach(
                line.segments,
                (segment: TerminalAnsiSegment): void => {
                  Span(segment.text)
                    .fontColor(segment.color)
                    .fontWeight(segment.bold
                      ? FontWeight.Bold
                      : FontWeight.Normal);
                },
                (
                  segment: TerminalAnsiSegment,
                  segmentIndex: number
                ): string =>
                  `${segmentIndex}-${segment.text}-${segment.color}-${segment.bold}`
              );
            }
              .width('100%').fontSize(15).fontFamily('monospace')
              .lineHeight(22).textAlign(TextAlign.Start)
              .copyOption(line.copyable
                ? CopyOptions.LocalDevice
                : CopyOptions.None)
          }, (line: TerminalLine, index: number): string => `${index}-${line.text}`)
        }.width('100%').alignItems(HorizontalAlign.Start)
      }
      .layoutWeight(1).width('100%').padding({ left: 18, right: 18, top: 16 })
      .onAreaChange((oldValue: Area, newValue: Area): void => {
        this.handleTerminalAreaChange(oldValue, newValue);
      })

      Row({ space: 8 }) {
        Text(this.shell.inputPrompt()).fontSize(15).fontColor('#7ee787').fontFamily('monospace')
          .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
        TextInput({
          text: this.command,
          placeholder: '',
          controller: this.inputController
        })
          .layoutWeight(1).height(32).fontSize(15).fontColor('#f8f8f2').fontFamily('monospace')
          .backgroundColor('#0d1117').border({ width: 0 })
          .type(this.shell.credentialPasswordActive()
            ? InputType.Password
            : InputType.Normal)
          .copyOption(this.shell.credentialPromptActive()
            ? CopyOptions.None
            : CopyOptions.LocalDevice)
          .onChange((value: string): void => {
            this.command = value;
            if (!this.shell.editorActive() &&
                !this.shell.credentialPromptActive()) {
              this.inputSession.edit(value);
            }
          })
          .onKeyEvent((event: KeyEvent): boolean =>
            this.handleInputKey(event))
          .onSubmit((): void => { this.executeCommand(); })
      }
      .width('100%').padding({ left: 18, right: 18, top: 10, bottom: 16 })
    }
    .width('100%').height('100%').backgroundColor('#0d1117')
  }
}
