LATEXMK = latexmk
CHAPTER ?=
DIRS = build/cli/chapters build/cli/appendices build/final/chapters build/final/appendices build/only/chapters build/only/appendices

.PHONY: dirs pdf final watch only open clean distclean lint words bibcheck todo

pdf: dirs
	$(LATEXMK) main.tex

final: dirs
	$(LATEXMK) -outdir=build/final final.tex

watch: dirs
	$(LATEXMK) -pvc main.tex

only: dirs
	@test -n "$(CHAPTER)" || { echo "usage: make only CHAPTER=chapters/06-ode-integration"; exit 1; }
	printf '\\def\\onlychapters{$(CHAPTER)}\n\\input{main}\n' > build/only.tex
	$(LATEXMK) -outdir=build/only build/only.tex

open:
	xdg-open build/cli/main.pdf

clean:
	$(LATEXMK) -c main.tex
	$(LATEXMK) -c -outdir=build/final final.tex
	-$(LATEXMK) -c -outdir=build/only build/only.tex

distclean:
	rm -rf build

lint:
	chktex -q -I0 -l .chktexrc main.tex preamble.tex macros.tex chapters/*.tex chapters/sections/*.tex appendices/*.tex

words:
	texcount -inc -total -sum main.tex

bibcheck:
	biber --tool --validate-datamodel --output-file=build/biber-tool.bib --logfile=build/biber-tool.blg references.bib references_unverified.bib

todo:
	@echo "stub sections (planned): $$(grep -rho '\\planned{' chapters appendices | wc -l)"
	@echo "TODO notes:              $$(grep -rho '\\TODO{' main.tex chapters appendices | wc -l)"

dirs:
	@mkdir -p $(DIRS)
