#!/bin/bash

mkdir -p out/ && cd out/
mkdir -p theory/
mkdir -p generalrelativity
cp ../theory/ref-math.bib theory/ref-math.bib
cp ../theory/ref-rendering.bib theory/ref-rendering.bib
cd ..

pdflatex --output-directory="out/" book
bibtex theory/math
bibtex theory/rendering
bibtex generalrelativity/curvature
bibtex generalrelativity/energy
bibtex generalrelativity/equations
bibtex generalrelativity/solutions
bibtex generalrelativity/universe
makeglossaries book
makeindex book
pdflatex --output-directory="out/" book
pdflatex --output-directory="out/" book
