#!/usr/bin/env bash
# Stage the GDML->OBJ converter for the Docker build:
#   - the `gdml2obj` binary (compiled against your local Geant4)
#   - every non-system shared library it needs (Geant4 + TBB)
# Run this on the host that has Geant4 installed, before `docker compose build`.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -x ./gdml2obj ]; then
  echo "error: ./gdml2obj not found — build it first (see cloud/README.md '3D-превью')" >&2
  exit 1
fi

rm -rf stage
mkdir -p stage/lib
cp -a gdml2obj stage/gdml2obj

# Copy custom (non-system) shared libraries, dereferencing symlinks so each
# file is self-contained under its SONAME basename.
ldd gdml2obj | awk '/=> \// {print $3}' | sort -u | while read -r lib; do
  case "$lib" in
    /lib/*|/usr/lib/*) continue ;;
  esac
  cp -aL "$lib" "stage/lib/$(basename "$lib")"
done

echo "Staged $(ls stage/lib | wc -l) libraries into stage/lib"
