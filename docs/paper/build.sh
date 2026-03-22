#!/bin/bash
mkdir -p out
pdflatex -interaction=nonstopmode -output-directory=out main.tex
biber --input-directory=out --output-directory=out main
makeglossaries -d out main
pdflatex -interaction=nonstopmode -output-directory=out main.tex
pdflatex -interaction=nonstopmode -output-directory=out main.tex
