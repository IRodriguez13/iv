#!/usr/bin/env bash
# install.sh — build and install iv
set -euo pipefail

PREFIX="${PREFIX:-${HOME}/.local}"
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
	cat <<EOF
Usage: ./install.sh [options]

Options:
  --prefix PATH   install prefix (default: \$HOME/.local)
  --uninstall     remove installed binary, man page, and completions
  -h, --help      show this help

Examples:
  ./install.sh
  PREFIX=/usr/local ./install.sh
  ./install.sh --uninstall
EOF
}

uninstall=false
while [[ $# -gt 0 ]]; do
	case "$1" in
		--prefix)
			PREFIX="$2"
			shift 2
			;;
		--uninstall)
			uninstall=true
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "install.sh: unknown option: $1" >&2
			usage >&2
			exit 1
			;;
	esac
done

cd "$SRC_DIR"

if $uninstall; then
	make PREFIX="$PREFIX" uninstall
	echo "Uninstalled from $PREFIX"
	exit 0
fi

make clean all
make PREFIX="$PREFIX" install

echo ""
echo "Installed:"
echo "  $PREFIX/bin/iv"
echo "  $PREFIX/share/man/man1/iv.1"
echo "  $PREFIX/share/man/es/man1/iv.1"
echo "  $PREFIX/share/bash-completion/completions/iv"
echo "  $PREFIX/share/zsh/site-functions/_iv"
echo "  $PREFIX/share/fish/vendor_completions.d/iv.fish"
echo ""
echo "Shell completions (bash / zsh / fish):"
echo "  bash: needs bash-completion package"
echo "  zsh:  fpath=(\$HOME/.local/share/zsh/site-functions \$fpath) before compinit"
echo "  fish: auto-loads vendor_completions.d"
echo ""
if command -v iv >/dev/null 2>&1; then
	installed="$("$PREFIX/bin/iv" --version 2>/dev/null | head -1)"
	active="$(iv --version 2>/dev/null | head -1)"
	if [[ "$installed" != "$active" ]]; then
		echo "WARNING: \`iv\` in PATH is not the one just installed."
		echo "  installed: $PREFIX/bin/iv → $installed"
		echo "  active:    $(command -v iv) → $active"
		echo "  Put \$PREFIX/bin before /usr/bin in PATH, or remove the old binary (often /usr/bin/iv)."
	fi
fi
echo ""
echo "Ensure \$PREFIX/bin is in PATH (before /usr/bin if both exist)."
