#!/bin/bash
# Build G4CARE Apptainer container
# Usage: ./build_container.sh

set -e

DEF_FILE="$(dirname "$0")/G4CARE.def"
SIF_FILE="$(dirname "$0")/G4CARE.sif"

echo "=== Building G4CARE Apptainer container ==="
echo "Definition: $DEF_FILE"
echo "Output:     $SIF_FILE"
echo ""

# Check Apptainer
if ! command -v apptainer &>/dev/null; then
    echo "ERROR: apptainer not found. Install it first:"
    echo "  sudo apt install apptainer"
    exit 1
fi

# --- Source paths (adjust to your environment) ---
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
G4CARE_BINARY="${G4CARE_BINARY:-$REPO_DIR/build/G4CARE}"
GEANT4_INSTALL_DIR="${GEANT4_INSTALL_DIR:-/path/to/geant4-install}"
GEANT4_DATA_DIR="${GEANT4_DATA_DIR:-/path/to/geant4-data}"
ROOT_INSTALL_DIR="${ROOT_INSTALL_DIR:-/path/to/root-install}"

# Check source directories exist
for dir in \
    "$GEANT4_INSTALL_DIR" \
    "$GEANT4_DATA_DIR" \
    "$ROOT_INSTALL_DIR" \
    "$G4CARE_BINARY"; do
    if [ ! -e "$dir" ]; then
        echo "ERROR: Source path not found: $dir"
        echo "       Set GEANT4_INSTALL_DIR, GEANT4_DATA_DIR, ROOT_INSTALL_DIR"
        echo "       and build G4CARE (build/G4CARE) before running this script."
        exit 1
    fi
done

echo "Source paths verified OK."
echo ""

# Clean old SIF if exists
if [ -f "$SIF_FILE" ]; then
    echo "Removing old $SIF_FILE ..."
    rm -f "$SIF_FILE"
fi

echo "Building container (this may take 10-20 minutes)..."
echo ""

apptainer build "$SIF_FILE" "$DEF_FILE"

echo ""
echo "=== Done ==="
echo "Container size: $(du -h "$SIF_FILE" | cut -f1)"
echo ""
echo "Transfer to target machine and run:"
echo "  apptainer run G4CARE.sif /path/to/config.yaml"