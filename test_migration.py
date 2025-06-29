#!/usr/bin/env python3
"""Test script to verify the Python initialization migration works."""

import dpcore

def test_filter_creation():
    """Test basic filter creation with the new initialization."""
    print("Testing BpFilterBase creation...")
    
    # Test creating a base filter with required capacity_exp argument
    base_filter = dpcore.BpFilterBase(10)
    print(f"✓ Base filter created successfully")
    
    # Test creating a base filter with both arguments (dtype as keyword-only)
    base_filter2 = dpcore.BpFilterBase(10, dtype=dpcore.DTYPE_FLOAT)
    print(f"✓ Base filter with dtype created successfully")
    
    # Test creating a Python filter 
    py_filter = dpcore.BpFilterPy(10, dtype=dpcore.DTYPE_FLOAT)
    print(f"✓ Python filter created successfully")
    
    print("All tests passed!")

if __name__ == "__main__":
    test_filter_creation()