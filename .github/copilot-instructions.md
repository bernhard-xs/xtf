# Repository Instructions for GitHub Copilot

Run "make" in the repository root to build all code, documentation, and tests.

Test your changes by actually running the tests. See PYTHON-TESTS-HOWTO.md for instructions on running the Python-hosted tests.

Only after building and running tests should you run the final validation command for Copilot-authored changes:

This repository uses `pre-commit` in CI.

It checks for code formatting, linting, and other issues in code,
build system, documentation, and tests. It is configured to run on
all files in the repository.

Among other checks, it runs:
- Adds missing trailing newlines to all files (add a trailing newline yourself)
- Removes trailing whitespace from all files.
- `isort`, `black`, and `flake8` for Python code.

The full list of checks is in the `.pre-commit-config.yaml` file in the
repository.

After making code, build-system, documentation, or test changes in this
repository, run:

```sh
SKIP=git-diff pre-commit run -av
```

Treat this as the final validation command for Copilot-authored changes.
If the command is unavailable or cannot complete in the current environment,
report that clearly along with any narrower checks that were run instead
and tell a human reviewer to install pre-commit using `pip install pre-commit`
and run the full command before merging.
