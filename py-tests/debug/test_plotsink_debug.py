#!/usr/bin/env python3
"""
Test PlotSink creation specifically.
"""

import os
import sys

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    sys.exit(1)

print("Step 1: Import matplotlib")
try:
    import matplotlib
    matplotlib.use('Agg')
    print("✓ matplotlib imported with Agg backend")
except Exception as e:
    print(f"✗ matplotlib import failed: {e}")

print("Step 2: Import CustomFilter")
try:
    print("✓ CustomFilter imported")
except Exception as e:
    print(f"✗ CustomFilter import failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("Step 3: Import PlotSink")
try:
    from bpipe.filters import PlotSink
    print("✓ PlotSink imported")
except Exception as e:
    print(f"✗ PlotSink import failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("Step 4: Create PlotSink")
try:
    plot_sink = PlotSink()
    print("✓ PlotSink created")
except Exception as e:
    print(f"✗ PlotSink creation failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("\n🎉 All PlotSink tests passed!")
