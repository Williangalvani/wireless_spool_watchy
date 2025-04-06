#!/bin/bash
# Debug build script for Watchy LVGL project

echo "========================================"
echo "Building Watchy LVGL project in verbose mode"
echo "========================================"

# Run PlatformIO build with verbose output
pio run -e esp32_watchy -v

# Check build result
if [ $? -eq 0 ]; then
  echo "========================================"
  echo "Build successful! Connect your Watchy to upload."
  echo "To upload: pio run -e esp32_watchy -t upload"
  echo "To monitor: pio run -e esp32_watchy -t monitor"
  echo "========================================"
else
  echo "========================================"
  echo "Build failed. Check the error messages above."
  echo "========================================"
fi 