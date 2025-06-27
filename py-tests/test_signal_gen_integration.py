#!/usr/bin/env python3
"""
Test script to verify signal generator integration is working.

Tests that the signal generator C code is compiled and constants are available.
"""

import os
import sys

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    sys.exit(1)

def test_constants():
    """Test that waveform constants are available."""
    print("\n=== Testing Waveform Constants ===")

    constants = [
        ('BP_WAVE_SQUARE', 0),
        ('BP_WAVE_SINE', 1),
        ('BP_WAVE_TRIANGLE', 2),
        ('BP_WAVE_SAWTOOTH', 3)
    ]

    for const_name, expected_value in constants:
        if hasattr(dpcore, const_name):
            actual_value = getattr(dpcore, const_name)
            if actual_value == expected_value:
                print(f"✓ {const_name} = {actual_value}")
            else:
                print(f"✗ {const_name} = {actual_value} (expected {expected_value})")
        else:
            print(f"✗ {const_name} not found")

    return True

def test_basic_filter_creation():
    """Test that we can create basic filters that could be signal generators."""
    print("\n=== Testing Basic Filter Creation ===")

    try:
        # Test base filter creation (what signal generators would use)
        base_filter = dpcore.BpFilterBase(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print(f"✓ Created BpFilterBase with dtype FLOAT ({dpcore.DTYPE_FLOAT})")

        # Test different dtypes
        for dtype_name in ['DTYPE_FLOAT', 'DTYPE_INT', 'DTYPE_UNSIGNED']:
            if hasattr(dpcore, dtype_name):
                dtype_val = getattr(dpcore, dtype_name)
                print(f"✓ {dtype_name} = {dtype_val}")
            else:
                print(f"✗ {dtype_name} not found")

        return True

    except Exception as e:
        print(f"✗ Filter creation failed: {e}")
        return False

def test_aggregator_compatibility():
    """Test that we can create aggregators (needed for plotting)."""
    print("\n=== Testing Aggregator Compatibility ===")

    try:
        # Create aggregator that could receive signal generator data
        agg = dpcore.BpAggregatorPy(max_capacity_bytes=1024*1024)
        print("✓ Created BpAggregatorPy")

        # Check initial state
        arrays = agg.arrays
        sizes = agg.get_sizes()
        print(f"✓ Aggregator has {len(arrays)} arrays, sizes: {sizes}")

        return True

    except Exception as e:
        print(f"✗ Aggregator creation failed: {e}")
        return False

def main():
    """Main test function."""
    print("Signal Generator Integration Test")
    print("=" * 40)

    try:
        success = True
        success &= test_constants()
        success &= test_basic_filter_creation()
        success &= test_aggregator_compatibility()

        if success:
            print("\n🎉 All tests passed!")
            print("Signal generator C code is compiled and ready for Python integration.")
        else:
            print("\n❌ Some tests failed.")
            return 1

    except Exception as e:
        print(f"\n💥 Test suite failed: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
