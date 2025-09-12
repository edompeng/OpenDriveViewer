#!/bin/bash
# Find lupdate tool
if [ -z "$QT6_ROOT" ]; then
    echo "Error: QT6_ROOT environment variable not set."
    exit 1
fi

LUPDATE="$QT6_ROOT/bin/lupdate"
if [ ! -f "$LUPDATE" ]; then
    # Try common mac path if not in bin
    LUPDATE="$QT6_ROOT/macos/bin/lupdate"
fi

if [ ! -f "$LUPDATE" ]; then
    echo "Error: lupdate not found in $QT6_ROOT/bin or $QT6_ROOT/macos/bin"
    exit 1
fi

# Run lupdate on the project root
# Scan src/app and src/utility for tr() strings
$LUPDATE src -ts translations/geoviewer_zh_CN.ts translations/geoviewer_en_US.ts
echo "Translations updated in translations/ folder."
