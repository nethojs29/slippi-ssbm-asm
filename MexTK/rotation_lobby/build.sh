#!/bin/bash
# Build script for Rotation Lobby Major Scene
# Compiles major.c (mjFunction) and minor.c (mnFunction) into RotationLobby.dat
#
# Both mjFunction and mnFunction live in the same .dat file.
# The m-ex loader reads mjFunction for the major scene lifecycle,
# and Load Minor Scene File reads mnFunction for the minor scene callbacks.

set -e

TKPATH="../../../m-ex/MexTK/"
MEXTK="MexTK.exe"  # Runs via Wine on macOS

# Compile major.c with mjFunction symbol table (creates .dat)
$MEXTK -ff -i "major.c" -s mjFunction -t "${TKPATH}mjFunction.txt" -l "${TKPATH}melee.link" -dat "RotationLobby.dat" -ow

# Compile minor.c with mnFunction symbol table into same .dat
$MEXTK -ff -i "minor.c" -s mnFunction -t "${TKPATH}mnFunction.txt" -l "${TKPATH}melee.link" -dat "RotationLobby.dat"

# Trim unused data from .dat
$MEXTK -trim "RotationLobby.dat"

echo "Built RotationLobby.dat successfully"
