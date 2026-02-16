#!/bin/sh
# Remove iv from /usr/bin

set -e

if [ -f /usr/bin/iv ]; then
    echo "Removing iv from /usr/bin..."
    sudo rm -f /usr/bin/iv
    echo "iv uninstalled successfully."
else
    echo "iv is not installed in /usr/bin."
fi
