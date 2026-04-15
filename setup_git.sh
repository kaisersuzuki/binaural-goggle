#!/bin/bash
# First-time setup: init git repo and push to GitHub.
# Run from inside the binaural-goggle/ directory.

set -e

if [ -d ".git" ]; then
  echo "Repo already initialized."
  exit 0
fi

echo "→ Initializing git repo..."
git init -b main
git add .
git commit -m "Initial commit: hardware, firmware, docs (v7 enclosure, schematic rev 0.2)"

echo ""
echo "→ Now create an empty repo on GitHub (no README, no .gitignore, no license — we have those)."
echo "  Suggested name: binaural-goggle"
echo "  https://github.com/new"
echo ""
echo "→ Then run these commands (replace YOUR_USERNAME):"
echo ""
echo "  git remote add origin https://github.com/YOUR_USERNAME/binaural-goggle.git"
echo "  git push -u origin main"
echo ""
