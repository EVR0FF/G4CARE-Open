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

# Check source directories exist
for dir in \
    /home/ever/software/geant4-11.4.0/geant4-v11.4.0-install \
    /home/ever/software/geant4-11.4.0/data \
    /home/ever/software/ROOT/install \
    /home/ever/GProjects/G4CARE/build/G4CARE; do
    if [ ! -e "$dir" ]; then
        echo "ERROR: Source path not found: $dir"
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