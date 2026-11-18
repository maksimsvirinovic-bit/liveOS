# UI Studio

UI Studio reads and writes plain-text `.UI` or `.SCR` files. In Files, opening either extension opens the source in UI Studio. The editor's path field controls the destination; **Save** truncates and rewrites that file. **Preview** and **Run** render the current source in the preview pane. `Run` currently runs the same preview interpreter; scripts are not native executables and do not execute arbitrary C, shell, or Python code.

## Supported commands

Each command occupies one line. Unknown or malformed lines are ignored.

```text
<rect x=12 y=12 w=140 h=72 color=0x00385070>
<text x=20 y=24 color=0x00FFFFFF>Hello</text>
{py: 6 * 7}
```

`<rect>` requires `x`, `y`, `w`, `h`, and `color`. Width and height must be positive. The rectangle is clipped to the preview canvas.

`<text>` requires `x`, `y`, and `color`; the text is the content after `>` through the end of the line. Text is drawn with the built-in bitmap font.

`{py: ...}` is a small integer expression, not Python. It accepts decimal integers, unary minus, spaces, and `+`, `-`, `*`, `/`. Multiplication and division are evaluated before addition and subtraction. Division by zero and signed 32-bit overflow are rejected.

Coordinates, dimensions, and colors accept decimal numbers or `0x` hexadecimal values. Attribute values cannot contain spaces. Commands are bounded by the editor's fixed text capacity; this is intentionally a small preview format, not a general-purpose application runtime.
