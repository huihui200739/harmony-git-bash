import { describe, expect, it } from '@ohos/hypium';
import {
  measureTerminalViewport,
  parseTerminalAnsi,
  TerminalAnsiText,
  TerminalViewport
} from '../main/ets/terminal/TerminalAnsi';

export default function terminalAnsiTest(): void {
  describe('TerminalAnsi', (): void => {
    it('keeps plain output and its default terminal color', 0, (): void => {
      const parsed: TerminalAnsiText =
        parseTerminalAnsi('plain\ntext', '#d8dee9');

      expect(parsed.text).assertEqual('plain\ntext');
      expect(parsed.segments.length).assertEqual(1);
      expect(parsed.segments[0].color).assertEqual('#d8dee9');
      expect(parsed.segments[0].bold).assertFalse();
    });

    it('renders standard ANSI foreground colors and bold resets', 0,
      (): void => {
        const parsed: TerminalAnsiText = parseTerminalAnsi(
          '\u001b[31mred\u001b[1;32m bold\u001b[0m plain',
          '#d8dee9'
        );

        expect(parsed.text).assertEqual('red bold plain');
        expect(parsed.segments.length).assertEqual(3);
        expect(parsed.segments[0].color).assertEqual('#ff7b72');
        expect(parsed.segments[0].bold).assertFalse();
        expect(parsed.segments[1].color).assertEqual('#7ee787');
        expect(parsed.segments[1].bold).assertTrue();
        expect(parsed.segments[2].color).assertEqual('#d8dee9');
        expect(parsed.segments[2].bold).assertFalse();
      });

    it('supports xterm 256 colors and ANSI truecolor values', 0,
      (): void => {
        const parsed: TerminalAnsiText = parseTerminalAnsi(
          '\u001b[38;5;196mred\u001b[38;2;1;2;3m rgb',
          '#d8dee9'
        );

        expect(parsed.text).assertEqual('red rgb');
        expect(parsed.segments.length).assertEqual(2);
        expect(parsed.segments[0].color).assertEqual('#ff0000');
        expect(parsed.segments[1].color).assertEqual('#010203');
      });

    it('applies carriage return, cursor movement and line erasure', 0,
      (): void => {
        const progress: TerminalAnsiText = parseTerminalAnsi(
          'Downloading 10%\r\u001b[2KDownloading 100%',
          '#d8dee9'
        );
        const moved: TerminalAnsiText = parseTerminalAnsi(
          'old\nsecond\u001b[1A\r\u001b[2Knew',
          '#d8dee9'
        );
        const overwritten: TerminalAnsiText = parseTerminalAnsi(
          'abc\u001b[2DXY',
          '#d8dee9'
        );

        expect(progress.text).assertEqual('Downloading 100%');
        expect(moved.text).assertEqual('new\nsecond');
        expect(overwritten.text).assertEqual('aXY');
      });

    it('converts the ArkUI terminal area into stable rows and columns', 0,
      (): void => {
        const viewport: TerminalViewport =
          measureTerminalViewport(756, 528, 36);
        const minimum: TerminalViewport =
          measureTerminalViewport(0, 0, 36);

        expect(viewport.width).assertEqual(720);
        expect(viewport.height).assertEqual(528);
        expect(viewport.columns).assertEqual(80);
        expect(viewport.rows).assertEqual(24);
        expect(minimum.columns).assertEqual(1);
        expect(minimum.rows).assertEqual(1);
      });
  });
}
