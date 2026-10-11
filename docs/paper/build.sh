#!/bin/env sh

OUTDIR="build"

mkdir -p $OUTDIR
pdflatex -interaction=nonstopmode -output-directory=$OUTDIR main.tex
biber --input-directory=$OUTDIR --output-directory=$OUTDIR main
makeglossaries -d $OUTDIR main
pdflatex -interaction=nonstopmode -output-directory=$OUTDIR main.tex
pdflatex -interaction=nonstopmode -output-directory=$OUTDIR main.tex
