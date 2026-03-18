# Contributing

## Development setup

After cloning, activate the pre-commit hook:

```bash
git config core.hooksPath .githooks
```

This enables the hook in [`.githooks/pre-commit`](.githooks/pre-commit) which blocks commits containing:

- personal information (absolute paths, usernames)
- newly added `std::filesystem` usage in staged C/C++ changes

Use `PathHelper`/`FileIO` helpers instead for path and file operations.

## CI policy: no `std::filesystem`

CI enforces this policy repository-wide using [scripts/check_no_filesystem.sh](scripts/check_no_filesystem.sh).
The check blocks both `#include <filesystem>` and `std::filesystem` usage in tracked C/C++ files.

Use these replacements instead:

- `PathHelper::normalizePath(path)`
- `PathHelper::joinPath(base, component)`
- `PathHelper::createDirectories(path)`
- `PathHelper::removeAll(path)`
- `PathHelper::tempDirectory()`
- `FileIO::File` for file reads/writes
