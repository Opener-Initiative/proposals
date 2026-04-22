# *Opener* Specification

This repository contains functional and technical specification documents of the Opener Initiative's DECT-2020 NR (DECT NR+) protocol stack implementation. To propose features and requirements to be included in the *Opener* specification, create a pull request and reference [issue #1](https://github.com/Opener-Initiative/specification/issues/1).

## 🚀 Build Locally

```bash
# Install dependencies
pip install -r requirements.txt
# or use uv
uv sync

# Build HTML
make html
# → open docs/_build/html/index.html
```

## ✍️ Contributing

1. create a feature branch.
2. Add or edit `.rst` files in the appropriate `docs/` subfolder.
3. Register new files in the nearest `toctree` directive.
4. Open a pull request referencing issue #1.

## 🔧 CI / CD

The workflow at `.github/workflows/docs.yml` builds the HTML documentation on
every push to `main` and deploys it to GitHub Pages automatically.

> **Prerequisite:** In the repository settings under *Settings → Pages*,
> set the source to **GitHub Actions**.