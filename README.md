# Proposals

> [!IMPORTANT]
> This repository is in a draft state and is currently a space for collecting input documents for the *Opener* stack implementation. It is inappropriate to use it as reference material or to cite it other than as "work in progress."

This repository is where ideas and proposals for the Opener Initiative's DECT-2020 NR (DECT NR+) protocol stack implementation are put forward, discussed, and refined. The implementation and its documentation are maintained separately.

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

1. Create a feature branch.
2. Add or edit `.rst` files in the appropriate `docs/` subfolder.
3. Register new files in the nearest `toctree` directive.
4. Open a pull request referencing [issue #1](https://github.com/Opener-Initiative/proposals/issues/1). Discussion happens in the pull request and any linked issues.

## 🔧 CI / CD

The workflow at `.github/workflows/docs.yml` builds the HTML documentation on
every push to `main` and deploys it to GitHub Pages automatically.

> **Prerequisite:** In the repository settings under *Settings → Pages*,
> set the source to **GitHub Actions**.