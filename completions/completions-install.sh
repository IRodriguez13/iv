#!/bin/bash
set -e

sudo make install
source /etc/bash_completion.d/iv
echo "✓ iv installed and activated for current session"

# Opcional: agregar a bashrc
if ! grep -q "source /etc/bash_completion.d/iv" ~/.bashrc; then
    echo "source /etc/bash_completion.d/iv" >> ~/.bashrc
    echo "✓ Added to ~/.bashrc (will auto-activate in new shells)"
fi
