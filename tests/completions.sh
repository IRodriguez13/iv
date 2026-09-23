#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

bash -n "$ROOT/completions/bash/iv"

if command -v zsh >/dev/null 2>&1; then
	zsh -f -c "autoload -Uz compinit; compinit -u; source '$ROOT/completions/zsh/_iv'"
fi

if command -v fish >/dev/null 2>&1; then
	fish --no-config -c "source '$ROOT/completions/fish/iv.fish'"
fi

echo "completions OK"
