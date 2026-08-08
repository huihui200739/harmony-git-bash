import { GitShell } from '../git/GitShell';

interface TerminalLine {
  text: string;
  tone: string;
}

@Entry
@Component
struct Index {
  @State private command: string = '';
  @State private lines: TerminalLine[] = [];
  private shell: GitShell = new GitShell();

  aboutToAppear(): void {
    this.lines = [
      { text: 'Welcome to Harmony Git Bash', tone: '#d8dee9' },
      { text: 'Git for Windows compatibility shell for HarmonyOS PC', tone: '#8fbcbb' },
      { text: 'Type "help" for supported commands.', tone: '#aab2c0' },
      { text: '', tone: '#d8dee9' }
    ];
  }

  private executeCommand(): void {
    const input: string = this.command.trim();
    if (input.length === 0) {
      return;
    }
    const nextLines: TerminalLine[] = this.lines.slice();
    nextLines.push({ text: `${this.shell.prompt()}$ ${input}`, tone: '#f8f8f2' });
    const output: string = this.shell.execute(input);
    if (output === '__CLEAR__') {
      this.lines = [];
      this.command = '';
      return;
    }
    if (output.length > 0) {
      nextLines.push({ text: output, tone: this.outputTone(output) });
    }
    nextLines.push({ text: '', tone: '#d8dee9' });
    this.lines = nextLines;
    this.command = '';
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
        Text('MINGW64:/home/harmony/workspace/demo-app')
          .fontSize(14).fontColor('#e6edf3').fontFamily('monospace').layoutWeight(1)
        Text('Harmony Git Bash 0.1.0')
          .fontSize(12).fontColor('#8b949e')
      }
      .height(40).width('100%').padding({ left: 14, right: 16 })
      .backgroundColor('#21262d')

      Scroll() {
        Column({ space: 4 }) {
          ForEach(this.lines, (line: TerminalLine): void => {
            Text(line.text)
              .width('100%').fontSize(15).fontColor(line.tone).fontFamily('monospace')
              .lineHeight(22).textAlign(TextAlign.Start)
          }, (line: TerminalLine, index: number): string => `${index}-${line.text}`)
        }.width('100%').alignItems(HorizontalAlign.Start)
      }
      .layoutWeight(1).width('100%').padding({ left: 18, right: 18, top: 16 })

      Row({ space: 8 }) {
        Text(`${this.shell.prompt()}$`).fontSize(15).fontColor('#7ee787').fontFamily('monospace')
          .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
        TextInput({ text: this.command, placeholder: '' })
          .layoutWeight(1).height(32).fontSize(15).fontColor('#f8f8f2').fontFamily('monospace')
          .backgroundColor('#0d1117').border({ width: 0 })
          .onChange((value: string): void => { this.command = value; })
          .onSubmit((): void => { this.executeCommand(); })
      }
      .width('100%').padding({ left: 18, right: 18, top: 10, bottom: 16 })
    }
    .width('100%').height('100%').backgroundColor('#0d1117')
  }
}
