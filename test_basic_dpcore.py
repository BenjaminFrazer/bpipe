#!/usr/bin/env python3
"""
Test basic dpcore functionality step by step.
"""

import dpcore
import traceback

def test_creation_only():
    """Test just creating filters."""
    print("Testing filter creation only...")
    try:
        filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        print("✓ BpFilterBase created")
        
        filter2 = dpcore.BpFilterPy(capacity_exp=10, dtype=2)
        print("✓ BpFilterPy created")
        
        # Test without any operations
        print("✓ Creation test passed")
        return True
    except Exception as e:
        print(f"✗ Creation failed: {e}")
        traceback.print_exc()
        return False

def test_connection_only():
    """Test connecting filters without starting."""
    print("\nTesting connection only...")
    try:
        filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        filter2 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        
        filter1.add_sink(filter2)
        print("✓ Filters connected")
        
        # Test disconnection
        filter1.remove_sink(filter2)
        print("✓ Filters disconnected")
        
        return True
    except Exception as e:
        print(f"✗ Connection test failed: {e}")
        traceback.print_exc()
        return False

def test_start_only():
    """Test starting a single filter without stopping."""
    print("\nTesting start only (no stop)...")
    try:
        filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        filter1.run()
        print("✓ Filter started (not stopping)")
        return True
    except Exception as e:
        print(f"✗ Start test failed: {e}")
        traceback.print_exc()
        return False

if __name__ == "__main__":
    print("Basic dpcore Testing")
    print("=" * 25)
    
    tests = [
        test_creation_only,
        test_connection_only,
        test_start_only,  # This one will leave a thread running
    ]
    
    for test in tests:
        if not test():
            break
    
    print("\n" + "=" * 25)
    print("Basic tests complete")
    print("Note: Last test leaves a thread running to avoid stop issues")