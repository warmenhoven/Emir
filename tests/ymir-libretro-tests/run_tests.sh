#!/bin/bash
# Run all tests for Ymir libretro core

cd "$(dirname "$0")"

if [ ! -d venv ]; then
    echo "Creating virtualenv..."
    python3 -m venv venv
    source venv/bin/activate
    pip install -r requirements.txt
else
    source venv/bin/activate
fi

# Run pytest with verbose output
pytest -v "$@"
