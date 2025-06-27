#!/usr/bin/env python3
"""
Minimal test to understand the stop issue in the simplest case.
"""

import dpcore
import time

print("Creating basic filter...")
filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
print("✓ Filter created")

print("Starting filter...")
filter1.run()
print("✓ Filter started")

print("Letting it run briefly...")
time.sleep(0.1)

print("Stopping filter...")
filter1.stop()
print("✓ Filter stopped successfully!")

print("Test completed without segfault!")