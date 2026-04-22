SPHINXOPTS  ?=
SPHINXBUILD ?= sphinx-build
SOURCEDIR   = docs
BUILDDIR    = docs/_build

.PHONY: help html clean

help:
	@echo "Available targets:"
	@echo "  html  – Generate HTML documentation"
	@echo "  clean – Delete build directory"

html:
	$(SPHINXBUILD) -b html $(SOURCEDIR) $(BUILDDIR)/html $(SPHINXOPTS)
	@echo "HTML generated: $(BUILDDIR)/html/index.html"

clean:
	rm -rf $(BUILDDIR)
