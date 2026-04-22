# Configuration file for the Sphinx documentation builder.


# -- Project information -------------------------------------------------------
project = "Opener Specification"
copyright = "2025, Opener Initiative"
author = "Opener Initiative"
release = "0.0.1"

# -- General configuration -----------------------------------------------------
extensions = [
    "sphinx.ext.autodoc",
    "sphinx.ext.intersphinx",
    "sphinx.ext.todo",
    "sphinx.ext.viewcode",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# Show TODO notes in output
todo_include_todos = True

# -- HTML output ---------------------------------------------------------------
html_theme = "sphinx_rtd_theme"
html_static_path = ["_static"]
html_theme_options = {
    "navigation_depth": 4,
    "titles_only": False,
}

html_css_files = ["custom.css"]
