#!/usr/bin/env python3
"""Test script to verify column widths display correctly on initial load"""

import subprocess
import time
import sys
import os

def test_column_widths():
    """Run the TUI and check if columns are properly sized"""
    print("Testing column width fix...")
    print("=" * 60)
    
    # Use XCAPTURE_DATADIR environment variable or default
    datadir = os.environ.get('XCAPTURE_DATADIR', '/home/tanel/dev/0xtools-next/xcapture/out')
    
    # Command to run
    cmd = [
        "../xtop",
        "-d", datadir,
        "--from", "2025-08-11T16:25:00",
        "--to", "2025-08-11T17:05:00",
        "--debuglog", "width_test.log"
    ]
    
    print(f"Running: {' '.join(cmd)}")
    print()
    print("Please check:")
    print("1. On initial load, columns should be properly sized")
    print("2. Content should NOT be truncated")
    print("3. You should NOT need to press 'r' to fix widths")
    print()
    print("Press Ctrl+C to stop the TUI when done checking")
    print("=" * 60)
    
    try:
        # Run the TUI
        subprocess.run(cmd)
    except KeyboardInterrupt:
        print("\nTest completed")
        
    # Check the debug log
    print("\nChecking debug log for column width info...")
    try:
        with open("width_test.log", "r") as f:
            for line in f:
                if "Added column:" in line and "width:" in line:
                    print(line.strip())
    except FileNotFoundError:
        print("Debug log not found")

if __name__ == "__main__":
    test_column_widths()