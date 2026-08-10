export interface TerminalAnsiSegment {
  text: string;
  color: string;
  bold: boolean;
}

export interface TerminalAnsiText {
  text: string;
  segments: TerminalAnsiSegment[];
}

export interface TerminalViewport {
  width: number;
  height: number;
  columns: number;
  rows: number;
}

interface TerminalCell {
  text: string;
  color: string;
  bold: boolean;
}

const ANSI_COLORS: string[] = [
  '#484f58',
  '#ff7b72',
  '#7ee787',
  '#d29922',
  '#79c0ff',
  '#d2a8ff',
  '#56d4dd',
  '#f0f6fc',
  '#6e7681',
  '#ffa198',
  '#56d364',
  '#e3b341',
  '#a5d6ff',
  '#d2a8ff',
  '#56d4dd',
  '#ffffff'
];

export function measureTerminalViewport(
  width: number,
  height: number,
  horizontalPadding: number = 0,
  characterWidth: number = 9,
  lineHeight: number = 22
): TerminalViewport {
  const availableWidth: number = Math.max(
    0,
    width - Math.max(0, horizontalPadding)
  );
  const availableHeight: number = Math.max(0, height);
  return {
    width: availableWidth,
    height: availableHeight,
    columns: Math.max(
      1,
      Math.floor(availableWidth / Math.max(1, characterWidth))
    ),
    rows: Math.max(
      1,
      Math.floor(availableHeight / Math.max(1, lineHeight))
    )
  };
}

export function parseTerminalAnsi(
  value: string,
  defaultColor: string
): TerminalAnsiText {
  return new TerminalAnsiParser(defaultColor).parse(value);
}

class TerminalAnsiParser {
  private lines: TerminalCell[][] = [[]];
  private cursorRow: number = 0;
  private cursorColumn: number = 0;
  private savedCursorRow: number = 0;
  private savedCursorColumn: number = 0;
  private color: string;
  private bold: boolean = false;
  private readonly defaultColor: string;

  constructor(defaultColor: string) {
    this.defaultColor = defaultColor;
    this.color = defaultColor;
  }

  parse(value: string): TerminalAnsiText {
    for (let index: number = 0; index < value.length; index++) {
      const character: string = value[index];
      if (character === '\u001b') {
        index = this.consumeEscape(value, index);
        continue;
      }
      if (character === '\n') {
        this.cursorRow++;
        this.cursorColumn = 0;
        this.ensureLine(this.cursorRow);
        continue;
      }
      if (character === '\r') {
        this.cursorColumn = 0;
        continue;
      }
      if (character === '\b') {
        this.cursorColumn = Math.max(0, this.cursorColumn - 1);
        continue;
      }
      if (character === '\t') {
        const nextStop: number =
          (Math.floor(this.cursorColumn / 8) + 1) * 8;
        while (this.cursorColumn < nextStop) {
          this.writeCharacter(' ');
        }
        continue;
      }
      if (character.charCodeAt(0) >= 32) {
        this.writeCharacter(character);
      }
    }
    return this.document();
  }

  private consumeEscape(value: string, start: number): number {
    if (start + 1 >= value.length) {
      return start;
    }
    const introducer: string = value[start + 1];
    if (introducer === '[') {
      let end: number = start + 2;
      while (end < value.length) {
        const code: number = value.charCodeAt(end);
        if (code >= 0x40 && code <= 0x7e) {
          this.applyCsi(
            value.slice(start + 2, end),
            value[end]
          );
          return end;
        }
        end++;
      }
      return value.length - 1;
    }
    if (introducer === ']') {
      let end: number = start + 2;
      while (end < value.length) {
        if (value[end] === '\u0007') {
          return end;
        }
        if (value[end] === '\u001b' &&
            value[end + 1] === '\\') {
          return end + 1;
        }
        end++;
      }
      return value.length - 1;
    }
    if (introducer === '7') {
      this.saveCursor();
    } else if (introducer === '8') {
      this.restoreCursor();
    } else if (introducer === 'c') {
      this.reset();
    }
    return start + 1;
  }

  private applyCsi(parameters: string, command: string): void {
    if (command === 'm') {
      this.applyGraphics(parameters);
      return;
    }
    if (command === 's') {
      this.saveCursor();
      return;
    }
    if (command === 'u') {
      this.restoreCursor();
      return;
    }
    const values: number[] = this.numericParameters(parameters);
    const amount: number = Math.max(1, values[0] ?? 1);
    if (command === 'A') {
      this.cursorRow = Math.max(0, this.cursorRow - amount);
    } else if (command === 'B') {
      this.cursorRow += amount;
      this.ensureLine(this.cursorRow);
    } else if (command === 'C') {
      this.cursorColumn += amount;
    } else if (command === 'D') {
      this.cursorColumn = Math.max(0, this.cursorColumn - amount);
    } else if (command === 'E') {
      this.cursorRow += amount;
      this.cursorColumn = 0;
      this.ensureLine(this.cursorRow);
    } else if (command === 'F') {
      this.cursorRow = Math.max(0, this.cursorRow - amount);
      this.cursorColumn = 0;
    } else if (command === 'G') {
      this.cursorColumn = amount - 1;
    } else if (command === 'H' || command === 'f') {
      this.cursorRow = Math.max(0, (values[0] ?? 1) - 1);
      this.cursorColumn = Math.max(0, (values[1] ?? 1) - 1);
      this.ensureLine(this.cursorRow);
    } else if (command === 'K') {
      this.eraseLine(values[0] ?? 0);
    } else if (command === 'J') {
      this.eraseDisplay(values[0] ?? 0);
    }
  }

  private applyGraphics(parameters: string): void {
    const values: number[] = parameters.length === 0
      ? [0]
      : this.numericParameters(parameters);
    for (let index: number = 0; index < values.length; index++) {
      const code: number = values[index];
      if (code === 0) {
        this.color = this.defaultColor;
        this.bold = false;
      } else if (code === 1) {
        this.bold = true;
      } else if (code === 22) {
        this.bold = false;
      } else if (code >= 30 && code <= 37) {
        this.color = ANSI_COLORS[code - 30];
      } else if (code === 39) {
        this.color = this.defaultColor;
      } else if (code >= 90 && code <= 97) {
        this.color = ANSI_COLORS[code - 90 + 8];
      } else if (code === 38 && values[index + 1] === 5 &&
          values[index + 2] !== undefined) {
        this.color = this.color256(values[index + 2]);
        index += 2;
      } else if (code === 38 && values[index + 1] === 2 &&
          values[index + 4] !== undefined) {
        this.color = this.rgbColor(
          values[index + 2],
          values[index + 3],
          values[index + 4]
        );
        index += 4;
      }
    }
  }

  private numericParameters(parameters: string): number[] {
    const normalized: string = parameters.startsWith('?')
      ? parameters.slice(1)
      : parameters;
    if (normalized.length === 0) {
      return [0];
    }
    return normalized.split(';').map(
      (value: string): number => {
        if (value.length === 0) {
          return 0;
        }
        const parsed: number = Number(value);
        return Number.isFinite(parsed) ? Math.floor(parsed) : 0;
      }
    );
  }

  private writeCharacter(character: string): void {
    const line: TerminalCell[] = this.ensureLine(this.cursorRow);
    while (line.length < this.cursorColumn) {
      line.push(this.cell(' ', this.defaultColor, false));
    }
    const nextCell: TerminalCell =
      this.cell(character, this.color, this.bold);
    if (this.cursorColumn < line.length) {
      line[this.cursorColumn] = nextCell;
    } else {
      line.push(nextCell);
    }
    this.cursorColumn++;
  }

  private eraseLine(mode: number): void {
    const line: TerminalCell[] = this.ensureLine(this.cursorRow);
    if (mode === 2) {
      this.lines[this.cursorRow] = [];
      return;
    }
    if (mode === 1) {
      const end: number = Math.min(this.cursorColumn, line.length - 1);
      for (let index: number = 0; index <= end; index++) {
        line[index] = this.cell(' ', this.defaultColor, false);
      }
      return;
    }
    if (this.cursorColumn < line.length) {
      line.splice(this.cursorColumn);
    }
  }

  private eraseDisplay(mode: number): void {
    if (mode === 2 || mode === 3) {
      this.lines = [[]];
      this.ensureLine(this.cursorRow);
      return;
    }
    if (mode === 1) {
      for (let row: number = 0; row < this.cursorRow; row++) {
        this.lines[row] = [];
      }
      this.eraseLine(1);
      return;
    }
    this.eraseLine(0);
    this.lines.splice(this.cursorRow + 1);
  }

  private saveCursor(): void {
    this.savedCursorRow = this.cursorRow;
    this.savedCursorColumn = this.cursorColumn;
  }

  private restoreCursor(): void {
    this.cursorRow = this.savedCursorRow;
    this.cursorColumn = this.savedCursorColumn;
    this.ensureLine(this.cursorRow);
  }

  private reset(): void {
    this.lines = [[]];
    this.cursorRow = 0;
    this.cursorColumn = 0;
    this.savedCursorRow = 0;
    this.savedCursorColumn = 0;
    this.color = this.defaultColor;
    this.bold = false;
  }

  private ensureLine(row: number): TerminalCell[] {
    while (this.lines.length <= row) {
      this.lines.push([]);
    }
    return this.lines[row];
  }

  private document(): TerminalAnsiText {
    const segments: TerminalAnsiSegment[] = [];
    for (let row: number = 0; row < this.lines.length; row++) {
      for (const cell of this.lines[row]) {
        this.appendSegment(
          segments,
          cell.text,
          cell.color,
          cell.bold
        );
      }
      if (row + 1 < this.lines.length) {
        this.appendSegment(
          segments,
          '\n',
          this.defaultColor,
          false
        );
      }
    }
    if (segments.length === 0) {
      segments.push({
        text: '',
        color: this.defaultColor,
        bold: false
      });
    }
    return {
      text: segments.map(
        (segment: TerminalAnsiSegment): string => segment.text
      ).join(''),
      segments
    };
  }

  private appendSegment(
    segments: TerminalAnsiSegment[],
    text: string,
    color: string,
    bold: boolean
  ): void {
    const previous: TerminalAnsiSegment | undefined =
      segments[segments.length - 1];
    if (previous !== undefined &&
        previous.color === color &&
        previous.bold === bold) {
      previous.text += text;
      return;
    }
    segments.push({ text, color, bold });
  }

  private cell(
    text: string,
    color: string,
    bold: boolean
  ): TerminalCell {
    return { text, color, bold };
  }

  private color256(value: number): string {
    const index: number = Math.max(0, Math.min(255, value));
    if (index < ANSI_COLORS.length) {
      return ANSI_COLORS[index];
    }
    if (index >= 232) {
      const component: number = 8 + (index - 232) * 10;
      return this.rgbColor(component, component, component);
    }
    const cube: number = index - 16;
    const red: number = Math.floor(cube / 36);
    const green: number = Math.floor(cube / 6) % 6;
    const blue: number = cube % 6;
    return this.rgbColor(
      this.colorCubeComponent(red),
      this.colorCubeComponent(green),
      this.colorCubeComponent(blue)
    );
  }

  private colorCubeComponent(value: number): number {
    return value === 0 ? 0 : 55 + value * 40;
  }

  private rgbColor(red: number, green: number, blue: number): string {
    return `#${this.hex(red)}${this.hex(green)}${this.hex(blue)}`;
  }

  private hex(value: number): string {
    const clamped: number = Math.max(
      0,
      Math.min(255, Math.floor(value))
    );
    return clamped.toString(16).padStart(2, '0');
  }
}
