#!/usr/bin/env python3
"""Render the demo walkthrough to an animated GIF and a still PNG.

Runs scripts/demo.sh with pacing disabled and colour forced on, captures the
ANSI stream, and paints it into a terminal-styled canvas with PIL. No external
recorder (asciinema/ffmpeg/agg) is required, so the media regenerates on any
host that has Python, Pillow, and the demo's own dependencies.

Outputs (under media/):
  demo.gif        scrolling terminal capture, ends on the merge-gate verdict
  demo_hero.png   the full session as one still, for a static post image
"""

import json
import os
import subprocess
import sys
import time

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEDIA = os.path.join(ROOT, "media")
FONT_REG = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

FONT_SIZE = 16
COLS = 96
ROWS = 30
PAD = 22
TITLEBAR = 34

BG = (13, 17, 23)
PANEL = (22, 27, 34)
CHROME = (33, 38, 45)
DEFAULT_FG = (201, 209, 217)
DIM_FG = (110, 118, 129)

STD = {
    30: (72, 79, 88), 31: (248, 81, 73), 32: (63, 185, 80), 33: (210, 153, 34),
    34: (88, 166, 255), 35: (188, 140, 255), 36: (57, 197, 207), 37: (177, 186, 196),
}
BRIGHT = {
    90: (139, 148, 158), 91: (255, 123, 114), 92: (86, 211, 100), 93: (227, 179, 65),
    94: (121, 192, 255), 95: (210, 168, 255), 96: (86, 212, 221), 97: (255, 255, 255),
}


class Style:
    __slots__ = ("fg", "bold", "dim")

    def __init__(self):
        self.fg = DEFAULT_FG
        self.bold = False
        self.dim = False

    def copy(self):
        s = Style()
        s.fg, s.bold, s.dim = self.fg, self.bold, self.dim
        return s


def parse_ansi(text):
    """Return a list of lines; each line is a list of (string, Style) runs."""
    lines = [[]]
    st = Style()
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "\x1b" and i + 1 < n and text[i + 1] == "[":
            j = i + 2
            while j < n and not text[j].isalpha():
                j += 1
            if j < n and text[j] == "m":
                params = text[i + 2:j]
                codes = [int(p) for p in params.split(";") if p != ""] or [0]
                for code in codes:
                    if code == 0:
                        st = Style()
                    elif code == 1:
                        st.bold = True
                    elif code == 2:
                        st.dim = True
                    elif code == 22:
                        st.bold = st.dim = False
                    elif code == 39:
                        st.fg = DEFAULT_FG
                    elif code in STD:
                        st.fg = STD[code]
                    elif code in BRIGHT:
                        st.fg = BRIGHT[code]
                st = st.copy()
            i = j + 1
            continue
        if ch == "\n":
            lines.append([])
        elif ch == "\r":
            pass
        elif ch == "\t":
            lines[-1].append(("    ", st.copy()))
        else:
            run = lines[-1]
            if run and run[-1][1] is st:
                run[-1] = (run[-1][0] + ch, st)
            else:
                run.append((ch, st))
        i += 1
    return lines


def load_fonts():
    return ImageFont.truetype(FONT_REG, FONT_SIZE), ImageFont.truetype(FONT_BOLD, FONT_SIZE)


def metrics(font):
    asc, desc = font.getmetrics()
    cell_w = int(round(font.getlength("M")))
    cell_h = asc + desc + 6
    return cell_w, cell_h, asc


def draw_window(img, w, h, title):
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=12, fill=PANEL)
    d.rounded_rectangle([0, 0, w - 1, TITLEBAR], radius=12, fill=CHROME)
    d.rectangle([0, TITLEBAR - 12, w - 1, TITLEBAR], fill=CHROME)
    for k, col in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]):
        cx = 18 + k * 20
        d.ellipse([cx, TITLEBAR // 2 - 6, cx + 12, TITLEBAR // 2 + 6], fill=col)
    tf = ImageFont.truetype(FONT_REG, 13)
    tw = tf.getlength(title)
    d.text(((w - tw) / 2, TITLEBAR / 2 - 8), title, font=tf, fill=(139, 148, 158))
    d.rectangle([PAD - 8, TITLEBAR + 6, w - PAD + 8, h - PAD + 8], fill=BG)
    return d


def render_view(lines, fonts, cell, title):
    reg, bold = fonts
    cell_w, cell_h, asc = cell
    rows = len(lines)
    w = COLS * cell_w + 2 * PAD + 16
    h = rows * cell_h + 2 * PAD + TITLEBAR + 12
    img = Image.new("RGB", (w, h), BG)
    d = draw_window(img, w, h, title)
    y = TITLEBAR + PAD
    for line in lines:
        x = PAD
        for text, st in line:
            font = bold if st.bold else reg
            col = DIM_FG if st.dim else st.fg
            d.text((x, y), text, font=font, fill=col)
            x += len(text) * cell_w
        y += cell_h
    return img


def write_cast(text, durations):
    """Emit an asciinema v2 cast reconstructed from the captured stream.

    The capture is non-interactive, so timings are synthesized from the same
    pacing the GIF uses. The result is a standard .cast that `asciinema play`
    replays and `agg`/`asciinema` convert to gif/mp4 on a host that has them.
    """
    raw_lines = text.split("\n")
    header = {
        "version": 2,
        "width": COLS,
        "height": ROWS,
        "timestamp": int(time.time()),
        "title": "network-chaos-harness demo",
        "env": {"TERM": "xterm-256color", "SHELL": "/bin/bash"},
    }
    events = []
    t = 0.4
    for idx, raw in enumerate(raw_lines):
        events.append([round(t, 3), "o", raw + "\r\n"])
        dur = durations[idx] if idx < len(durations) else 150
        t += max(dur, 60) / 1000.0
    cast_path = os.path.join(MEDIA, "demo.cast")
    with open(cast_path, "w", encoding="utf-8") as fh:
        fh.write(json.dumps(header) + "\n")
        for ev in events:
            fh.write(json.dumps(ev) + "\n")
    sys.stderr.write("wrote {} ({} events)\n".format(cast_path, len(events)))


def main():
    env = dict(os.environ)
    env["DEMO_PACE"] = "0"
    env["DEMO_FORCE_COLOR"] = "1"
    proc = subprocess.run(
        ["bash", os.path.join(ROOT, "scripts", "demo.sh")],
        cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
    )
    text = proc.stdout.decode("utf-8", "replace")
    lines = parse_ansi(text)
    if lines and not any(run for run in lines[-1]):
        lines.pop()

    fonts = load_fonts()
    cell = metrics(fonts[0])
    title = "network-chaos-harness  -  scripts/demo.sh"

    # Still: the full session in one frame.
    hero = render_view(lines, fonts, cell, title)
    hero_path = os.path.join(MEDIA, "demo_hero.png")
    hero.save(hero_path)
    sys.stderr.write("wrote {} ({}x{})\n".format(hero_path, hero.width, hero.height))

    def hold_for(line):
        flat = "".join(t for t, _ in line)
        if "engine exit code" in flat or "GATE:" in flat:
            return 2600
        if flat.strip().endswith("BGP speakers)") or "----" in flat:
            return 1100
        if flat.strip() == "":
            return 70
        return 150

    durations = [hold_for(line) for line in lines]
    write_cast(text, durations)

    # Animation: reveal line by line through a scrolling viewport.
    frames = []
    for k in range(1, len(lines) + 1):
        window = lines[:k][-ROWS:]
        padded = window + [[]] * (ROWS - len(window))
        frames.append(render_view(padded, fonts, cell, title))

    # Linger on the final verdict.
    frames.append(frames[-1])
    durations.append(3500)

    gif_path = os.path.join(MEDIA, "demo.gif")
    frames[0].save(
        gif_path, save_all=True, append_images=frames[1:],
        duration=durations, loop=0, optimize=True, disposal=2,
    )
    sys.stderr.write(
        "wrote {} ({} frames, {}x{})\n".format(
            gif_path, len(frames), frames[0].width, frames[0].height
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
