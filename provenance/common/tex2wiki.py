# SPDX-License-Identifier: MIT
"""tex2wiki.py -- derive <stem>.md and <stem>.txt from <stem>.tex.

The .tex is the source of truth; the Markdown copy and the femm.info
DokuWiki copy are generated from it, never edited by hand.  (md2tex.py
runs the other way, from a docs export, and must not be run over the
current .tex.)

pandoc does the prose, tables and inline markup.  Before it runs, this
script resolves what pandoc would drop or turn into dead links:

  \\cite{a,b}        -> [n, m], numbered in bibitem order
  \\ref{section}     -> the section's number
  \\eqref{eq:x}      -> (N) in Markdown; MathJax \\(\\ref{eqN}\\) on the wiki,
                       whose displays carry \\label{eqN} and are numbered
                       by MathJax, as on the other femm.info pages
  figures           -> an image plus a "Figure k:" caption line
  thebibliography   -> a numbered References section of "[n] ..." entries

Wiki conventions follow the femm.info corpus: ====title====,
===section===, ==subsection==, [[url|text]], {{:upload:<stem>:f.png}}.
Inline math stays TeX, as \\( ... \\), which the site's MathJax renders.

Backslashes are built from BS = chr(92) so the file survives being
pasted through shells.

Usage:  python tex2wiki.py [stem]      (default bhcurves)
"""
import datetime
import re
import subprocess
import sys

BS = chr(92)
STEM = sys.argv[1] if len(sys.argv) > 1 else "bhcurves"
DATE = datetime.date.today().strftime("%d %b %Y").lstrip("0")
WIKI_IMG = "{{:upload:" + STEM + ":%s|%s}}"


def arg(s, i):
    """Balanced {...} starting at s[i] == '{'; returns (content, end)."""
    assert s[i] == "{", s[i:i + 30]
    depth, j = 0, i
    while True:
        if s[j] == "{" and s[j - 1] != BS:
            depth += 1
        elif s[j] == "}" and s[j - 1] != BS:
            depth -= 1
            if depth == 0:
                return s[i + 1:j], j + 1
        j += 1


src = open(STEM + ".tex", encoding="utf-8").read()
TITLE, _ = arg(src, src.index(BS + "title{") + len(BS + "title"))
AUTHOR, _ = arg(src, src.index(BS + "author{") + len(BS + "author"))
body = src[src.index(BS + "begin{document}") + len(BS + "begin{document}"):
           src.index(BS + "end{document}")]
body = body.replace(BS + "maketitle", "")

# ---------------------------------------------------------- bibliography
cite_no = {}
if BS + "begin{thebibliography}" in body:
    b0 = body.index(BS + "begin{thebibliography}")
    b1 = body.index(BS + "end{thebibliography}") + len(BS + "end{thebibliography}")
    items = re.split(re.escape(BS) + r"bibitem\{", body[b0:b1])[1:]
    entries = []
    for n, it in enumerate(items, 1):
        key, rest = it.split("}", 1)
        rest = rest.replace(BS + "end{thebibliography}", "").strip()
        cite_no[key] = n
        entries.append("[%d] %s" % (n, rest))
    body = (body[:b0] + BS + "section{References}\n\n" + "\n\n".join(entries)
            + "\n" + body[b1:])


def cites(m):
    return "[" + ", ".join(str(cite_no[k.strip()]) for k in m.group(1).split(",")) + "]"


body = re.sub(re.escape(BS) + r"cite\{([^}]*)\}", cites, body)

# ------------------------------------------------------ section numbers
sec_no, out, i, s1, s2 = {}, [], 0, 0, 0
pat = re.compile(re.escape(BS) + r"(section|subsection)\{")
while True:
    m = pat.search(body, i)
    if not m:
        out.append(body[i:])
        break
    title, j = arg(body, m.end() - 1)
    if m.group(1) == "section":
        s1, s2 = s1 + 1, 0
        num = "%d" % s1
    else:
        s2 += 1
        num = "%d.%d" % (s1, s2)
    lm = re.match(re.escape(BS) + r"label\{([^}]*)\}", body[j:])
    if lm:
        sec_no[lm.group(1)] = num
        j += lm.end()
    title = " ".join(title.split())
    out.append(body[i:m.start()] + BS + m.group(1) + "{" + num + " " + title + "}")
    i = j
body = "".join(out)
body = re.sub(r"(?<![A-Za-z])" + re.escape(BS) + r"ref\{([^}]*)\}",
              lambda m: sec_no[m.group(1)], body)

# ---------------------------------------------------------- equations
eq_no = {}
epat = re.compile(re.escape(BS) + r"begin\{equation\}" + re.escape(BS)
                  + r"label\{([^}]*)\}(.*?)" + re.escape(BS) + r"end\{equation\}", re.S)
for n, m in enumerate(epat.finditer(body), 1):
    eq_no[m.group(1)] = n

# ------------------------------------------------------------ figures
figs = []


def fig(m):
    blk = m.group(0)
    f = re.search(re.escape(BS) + r"includegraphics\[.*?\]\{([^}]+)\}", blk, re.S).group(1)
    c = blk.index(BS + "caption{")
    cap, _ = arg(blk, c + len(BS + "caption"))
    cap = " ".join(cap.split()).replace(BS + "%", "%")
    figs.append((f, cap))
    return "\n\nFIGTOKEN%d\n\n" % len(figs)


body = re.sub(re.escape(BS) + r"begin\{figure\}.*?" + re.escape(BS) + r"end\{figure\}",
              fig, body, flags=re.S)


def variant(md):
    """The body with equations and their references resolved for one target."""
    def eq(m):
        n = eq_no[m.group(1)]
        if md:
            return BS + "[" + m.group(2).strip() + " " + BS + "tag{%d}" % n + BS + "]"
        return BS + "[" + BS + "label{EQLABEL%d}" % n + m.group(2).strip() + BS + "]"
    b = epat.sub(eq, body)
    b = re.sub(re.escape(BS) + r"eqref\{([^}]*)\}",
               lambda m: ("(%d)" if md else "EQREF%dZ") % eq_no[m.group(1)], b)
    return (BS + "documentclass{article}" + BS + "usepackage{amsmath}\n"
            + BS + "begin{document}\n" + b + BS + "end{document}\n")


def pandoc(tex, to, extra=()):
    r = subprocess.run(["pandoc", "-f", "latex", "-t", to, "--wrap=none"] + list(extra),
                       input=tex.encode("utf-8"), capture_output=True, check=True)
    return r.stdout.decode("utf-8").replace("\r\n", "\n")


# ------------------------------------------------------------ Markdown
md = pandoc(variant(True), "gfm", ["--shift-heading-level-by=1"])
for k, (f, cap) in enumerate(figs, 1):
    md = md.replace("FIGTOKEN%d" % k, "![Figure %d](%s)\n\n*Figure %d: %s*" % (k, f, k, cap))
md = "# %s\n\n%s · %s\n\n%s" % (TITLE, AUTHOR, DATE, md.lstrip())
open(STEM + ".md", "w", encoding="utf-8", newline="\n").write(md)

# ------------------------------------------------------------ DokuWiki
dw = pandoc(variant(False), "dokuwiki")


def heading(m):
    level = {6: 3, 5: 2}[len(m.group(1))]          # section ===, subsection ==
    return "=" * level + m.group(2).strip() + "=" * level


dw = re.sub(r"^(={5,6}) (.*?) ={5,6}$", heading, dw, flags=re.M)
dw = re.sub(r"\$\$(.+?)\$\$", lambda m: BS + "[ " + m.group(1).strip() + " " + BS + "]",
            dw, flags=re.S)
dw = re.sub(r"\$([^$\n]+?)\$", lambda m: BS + "(" + m.group(1) + BS + ")", dw)
dw = re.sub(r"EQLABEL(\d+)", r"eq\1", dw)
dw = re.sub(r"EQREF(\d+)Z", lambda m: BS + "(" + BS + "ref{eq%s}" % m.group(1) + BS + ")", dw)
for k, (f, cap) in enumerate(figs, 1):
    dw = dw.replace("FIGTOKEN%d" % k, WIKI_IMG % ((f.split("/")[-1],) * 2) + BS + BS + "\nFigure %d: %s" % (k, cap))
dw = "====%s====\n\n%s\n%s\n\n%s" % (TITLE, AUTHOR, DATE, dw.lstrip())
open(STEM + ".txt", "w", encoding="utf-8", newline="\n").write(dw)

print("%d references, %d equations, %d sections, %d figures" %
      (len(cite_no), len(eq_no), len(sec_no), len(figs)))
print("wrote %s.md, %s.txt" % (STEM, STEM))
