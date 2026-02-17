#!/bin/sh
# Remove iv from /usr/bin and man page from /usr/share/man/man1

set -e

removed=0

if [ -f /usr/bin/iv ]; then
    echo "Removing iv from /usr/bin..."
    sudo rm -f /usr/bin/iv
    removed=1
fi

if [ -f /usr/share/man/man1/iv.1.gz ] || [ -f /usr/share/man/man1/iv.1 ]; then
    echo "Removing man page..."
    sudo rm -f /usr/share/man/man1/iv.1.gz /usr/share/man/man1/iv.1
    removed=1
fi

if [ "$removed" = 1 ]; then
    echo "iv uninstalled successfully."
else
    echo "iv is not installed."
fi
