#!/bin/sh
cd "$(dirname "$0")" && make dirs >/dev/null && exec latexmk "$@" main.tex
