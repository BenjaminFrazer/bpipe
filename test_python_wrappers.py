#!/usr/bin/env python3
"""
Test script for Python wrapper implementation.

Tests basic functionality of the FilterFactory and CustomFilter classes.
"""

import sys
import traceback

def test_imports():
    """Test that all modules can be imported."""
    print("Testing imports...")
    try:
        import dpcore
        print("  ✓ dpcore imported successfully")
        
        from bpipe.filters import FilterFactory, CustomFilter
        print("  ✓ FilterFactory and CustomFilter imported successfully")
        
        from bpipe import FilterFactory as FF, CustomFilter as CF
        print("  ✓ Main package imports working")
        return True
    except Exception as e:
        print(f"  ✗ Import failed: {e}")
        traceback.print_exc()
        return False

def test_filter_creation():
    """Test creating filters with factory pattern."""
    print("\nTesting filter creation...")
    try:
        from bpipe.filters import FilterFactory, CustomFilter
        import numpy as np
        
        # Test signal generator creation
        signal_gen = FilterFactory.signal_generator(
            waveform='sawtooth',
            frequency=0.01,
            amplitude=100.0
        )
        print(f"  ✓ Signal generator created: {signal_gen.filter_type}")
        print(f"    Config: {signal_gen.config}")
        
        # Test passthrough creation
        passthrough = FilterFactory.passthrough()
        print(f"  ✓ Passthrough created: {passthrough.filter_type}")
        
        # Test custom filter creation
        def simple_transform(inputs):
            if not inputs or inputs[0] is None:
                return [np.array([])]
            return [inputs[0] * 2]  # Simple scaling
        
        custom = CustomFilter(simple_transform)
        print("  ✓ Custom filter created")
        
        return True
    except Exception as e:
        print(f"  ✗ Filter creation failed: {e}")
        traceback.print_exc()
        return False

def test_base_filter():
    """Test basic dpcore filter functionality."""
    print("\nTesting base filter functionality...")
    try:
        import dpcore
        
        # Create base filter
        base_filter = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        print("  ✓ Base filter created")
        
        # Test Python filter
        py_filter = dpcore.BpFilterPy(capacity_exp=10, dtype=2)  
        print("  ✓ Python filter created")
        
        return True
    except Exception as e:
        print(f"  ✗ Base filter test failed: {e}")
        traceback.print_exc()
        return False

def test_filter_connections():
    """Test connecting filters together."""
    print("\nTesting filter connections...")
    try:
        from bpipe.filters import FilterFactory
        
        # Create two filters
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        passthrough = FilterFactory.passthrough()
        
        # Test connection
        gen.add_sink(passthrough)
        print("  ✓ Filters connected successfully")
        
        # Test disconnection
        gen.remove_sink(passthrough)
        print("  ✓ Filters disconnected successfully")
        
        return True
    except Exception as e:
        print(f"  ✗ Filter connection test failed: {e}")
        traceback.print_exc()
        return False

def test_invalid_inputs():
    """Test handling of invalid inputs."""
    print("\nTesting invalid input handling...")
    try:
        from bpipe.filters import FilterFactory
        
        # Test invalid waveform
        try:
            FilterFactory.signal_generator('invalid_wave', 0.01, 50.0)
            print("  ✗ Should have failed with invalid waveform")
            return False
        except ValueError:
            print("  ✓ Invalid waveform correctly rejected")
        
        # Test invalid filter type
        try:
            from bpipe.filters import BuiltinFilter
            BuiltinFilter('invalid_type', {})
            print("  ✗ Should have failed with invalid filter type")
            return False
        except ValueError:
            print("  ✓ Invalid filter type correctly rejected")
        
        return True
    except Exception as e:
        print(f"  ✗ Invalid input test failed: {e}")
        traceback.print_exc()
        return False

def main():
    """Run all tests."""
    print("Python Wrapper Implementation Tests")
    print("=" * 40)
    
    tests = [
        test_imports,
        test_base_filter,
        test_filter_creation,
        test_filter_connections, 
        test_invalid_inputs,
    ]
    
    passed = 0
    total = len(tests)
    
    for test in tests:
        if test():
            passed += 1
    
    print(f"\n{'='*40}")
    print(f"Test Results: {passed}/{total} tests passed")
    
    if passed == total:
        print("🎉 All tests passed!")
        return 0
    else:
        print(f"❌ {total - passed} test(s) failed")
        return 1

if __name__ == "__main__":
    sys.exit(main())