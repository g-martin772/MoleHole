# Paper workflow

## Build

```
make            # draft PDF -> build/cli/main.pdf
make watch      # rebuild on every save (latexmk -pvc)
make final      # no planned/todo/opt markers -> build/final/final.pdf (final.tex = \def\finalbuild + \input{main})
make only CHAPTER=chapters/06-ode-integration   # one chapter, uses \includeonly -> build/only/only.pdf
make lint       # chktex
make words      # texcount
make todo       # number of unwritten sections and TODO notes
make bibcheck   # biber datamodel validation of both .bib files
make clean | distclean
```

Needs TeX Live with `pdflatex`, `biber`, `latexmk`, `makeglossaries`

## Writing conventions

- Labels: `ch:`, `sec:`, `subsec:`, `eq:`, `fig:`, `tab:`, `alg:`, `lst:`, `thm:`, `app:`. Always `\cref{}` / `\Cref{}`, never bare `\ref`
- One sentence per line (keeps diffs readable)
- Units with siunitx: `\qty{1.477}{\kilo\metre}`, `\num{6.674e-11}`, `\unit{\metre\per\second}`. Constants and masses: `\solarmass` is a unit, `\Msun` a math symbol
- Acronyms: `\gls{bh}` (first use expands), `\glspl{}`; add new ones to `acronyms.tex`
- Symbols: add to `symbols.tex`
- Cite with `\cite{key}`
- 
## Macros and environments

| Use | Command |
|---|---|
| derivatives | `\dd`, `\dv{f}{x}`, `\dv[2]{x}{t}`, `\pdv{f}{x}` |
| delimiters | `\abs{x}`, `\norm{v}`, `\avg{x}` |
| GR | `\cd`, `\Lie`, `\Chr{\mu}{\alpha}{\beta}`, `\tensor{R}{^\rho_{\sigma\mu\nu}}`, `\rs`, `\rg`, `\astar`, `\risco`, `\Lz` |
| vectors, numbers | `\vect{v}`, `\eu`, `\iu`, `\RR`, `\Exp`, `\Var`, `\Tr`, `\sgn` |
| theorem-like | `theorem`, `proposition`, `lemma`, `definition`, `example`, `remark` (numbered per chapter, cleveref names work) |
| highlight | `keyresult[Title]`, `optionalbox[Title]` (tcolorbox, breakable) |
| code | `lstlisting[language=GLSL]` (also `Luau`, `C++`), caption + `\label{lst:...}` |
| algorithms | `algorithm` + `algorithmic` (`\State`, `\For`, ...), `\label{alg:...}` |
| plots | `tikz`, `pgfplots` (`compat=1.18`), `tikz-3dplot` loaded; put reusable pictures in `figures/` |

## Draft helpers

| Command | Effect |
|---|---|
| `\planned{keywords}` | grey "Planned:" line under a section |
| `\plannedscope{...}` | grey "Scope:" line under a chapter title |
| `\section{Title\optmark}`, `\mentionmark` | "(opt.)" / "(mention)" in heading and TOC |
| `\TODO{text}` | orange inline note, collected in the list of todos after the contents |
| `\citeneeded` | red `[cite]` superscript |
| `\verify{0.998}` | red number that needs re-checking against a source |

Workflow for a section: write the text, delete its `\planned{}` line, run `make todo` to see what is left.

## Bibliography

Two files are loaded. 
An entry in `references_unverified.bib` prints with a "UNVERIFIED" note in the bibliography. 
