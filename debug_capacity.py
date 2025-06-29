#!/usr/bin/env python3
"""Debug the capacity_exp=30 issue."""

import dpcore

def test_capacity_30():
    """Test creating a filter with capacity_exp=30."""
    try:
        print("Testing capacity_exp=30...")
        filter1 = dpcore.BpFilterBase(capacity_exp=30, dtype=dpcore.DTYPE_FLOAT)
        print("✓ Filter with capacity_exp=30 created successfully")
        print(f"Filter object: {filter1}")
    except Exception as e:
        print(f"✗ Failed to create filter: {e}")

def test_capacity_20():
    """Test creating a filter with a more reasonable capacity_exp."""
    try:
        print("Testing capacity_exp=20...")
        filter1 = dpcore.BpFilterBase(capacity_exp=20, dtype=dpcore.DTYPE_FLOAT)
        print("✓ Filter with capacity_exp=20 created successfully")
        print(f"Filter object: {filter1}")
    except Exception as e:
        print(f"✗ Failed to create filter: {e}")

if __name__ == "__main__":
    test_capacity_20()
    test_capacity_30()