#!/bin/sh
# Install iv to /usr/bin and man page to /usr/share/man/man1

set -e
cd "$(dirname "$0")"

echo "Building iv..."
make

echo "Installing iv to /usr/bin..."
sudo cp iv /usr/bin/iv

echo "Installing man page to /usr/share/man/man1..."
sudo mkdir -p /usr/share/man/man1
sudo cp iv.1 /usr/share/man/man1/iv.1
sudo gzip -f /usr/share/man/man1/iv.1 2>/dev/null || true

echo "iv installed successfully."
