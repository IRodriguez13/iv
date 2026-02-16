#!/bin/sh
# Install iv to /usr/bin

set -e
cd "$(dirname "$0")"

echo "Building iv..."
make

echo "Installing iv to /usr/bin..."
sudo cp iv /usr/bin/iv

echo "iv installed successfully."
