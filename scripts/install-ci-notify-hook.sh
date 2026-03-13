#!/usr/bin/env bash
# Installs a post-push git hook that watches the GitHub Actions CI run
# triggered by the push and pops up a macOS notification on failure.
#
# Requirements: gh CLI authenticated (gh auth login)
# Usage: bash scripts/install-ci-notify-hook.sh

set -euo pipefail

HOOK="$(git rev-parse --git-dir)/hooks/post-push"

cat > "$HOOK" << 'EOF'
#!/usr/bin/env bash
# post-push: watch the latest CI run and notify on failure/success.
# Runs entirely in the background so the push is not blocked.
(
  sleep 5  # give GitHub a moment to register the run
  RUN_ID=$(gh run list --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null)
  if [[ -z "$RUN_ID" ]]; then exit 0; fi

  gh run watch "$RUN_ID" --exit-status > /dev/null 2>&1
  STATUS=$?

  if [[ $STATUS -ne 0 ]]; then
    osascript -e 'display notification "CI build FAILED — check GitHub Actions" with title "AISocks CI" sound name "Basso"'
  else
    osascript -e 'display notification "CI build passed" with title "AISocks CI" sound name "Glass"'
  fi
) &
disown
EOF

chmod +x "$HOOK"
echo "Hook installed at $HOOK"
echo "Make sure 'gh' is authenticated: gh auth status"
