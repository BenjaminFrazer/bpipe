#!/usr/bin/env python3
"""
Ultra-minimal segfault debugging.
"""

import sys
import os

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

print("Step 1: Import dpcore")
try:
    import dpcore
    print("✓ dpcore imported")
except Exception as e:
    print(f"✗ dpcore import failed: {e}")
    sys.exit(1)

print("Step 2: Create BpFilterBase")
try:
    base_filter = dpcore.BpFilterBase(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
    print("✓ BpFilterBase created")
except Exception as e:
    print(f"✗ BpFilterBase creation failed: {e}")
    sys.exit(1)

print("Step 3: Create BpAggregatorPy")
try:
    aggregator = dpcore.BpAggregatorPy(max_capacity_bytes=1024*1024)
    print("✓ BpAggregatorPy created")
except Exception as e:
    print(f"✗ BpAggregatorPy creation failed: {e}")
    sys.exit(1)

print("Step 4: Import CustomFilter")
try:
    from bpipe.filters import CustomFilter
    print("✓ CustomFilter imported")
except Exception as e:
    print(f"✗ CustomFilter import failed: {e}")
    sys.exit(1)

print("Step 5: Create CustomFilter")
try:
    def dummy_transform(inputs):
        import numpy as np
        return [np.array([1.0], dtype=np.float32)]
    
    custom_filter = CustomFilter(dummy_transform)
    print("✓ CustomFilter created")
except Exception as e:
    print(f"✗ CustomFilter creation failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("Step 6: Test basic operations")
try:
    # Just check that the object exists and has expected attributes
    print(f"  CustomFilter running: {custom_filter.running}")
    print(f"  CustomFilter has transform_func: {hasattr(custom_filter, 'transform_func')}")
    print("✓ Basic operations work")
except Exception as e:
    print(f"✗ Basic operations failed: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)

print("\n🎉 All basic tests passed - segfault is in threading or start operations")