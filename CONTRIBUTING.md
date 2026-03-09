# Contributing

## Development setup

After cloning, activate the pre-commit hook:

```bash
git config core.hooksPath .githooks
```

This enables the hook in [`.githooks/pre-commit`](.githooks/pre-commit) which blocks commits containing personal information (absolute paths, usernames).
