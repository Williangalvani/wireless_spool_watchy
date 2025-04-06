#!/bin/bash
# Simple build script for the Watchy LVGL project

echo "Building Watchy LVGL project..."
pio run -e esp32_watchy

if [ $? -eq 0 ]; then
  echo "Build successful! Connect your Watchy to upload."
  echo "To upload, run: pio run -e esp32_watchy -t upload"
  echo "To monitor serial output: pio run -e esp32_watchy -t monitor"
else
  echo "Build failed. Check the error messages above."
fi 