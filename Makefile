SPHINXOPTS  ?=
SPHINXBUILD ?= sphinx-build
SOURCEDIR   = docs
BUILDDIR    = docs/_build

.PHONY: help html serve clean

help:
	@echo "Available targets:"
	@echo "  html  – Generate HTML documentation"
	@echo "  serve – Live-reload docs at http://127.0.0.1:8080"
	@echo "  clean – Delete build directory"

html:
	$(SPHINXBUILD) -b html $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS)
	@echo "HTML generated: $(BUILDDIR)/html/index.html"

serve:
	sphinx-autobuild $(SOURCEDIR) $(BUILDDIR)/html --host 0.0.0.0 --port 8080

clean:
	rm -rf $(BUILDDIR)
