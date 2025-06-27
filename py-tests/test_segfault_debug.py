#!/usr/bin/env python3
"""
Segfault debugging script.

Systematically test different components to isolate the segfault source.
"""

import sys
import os
import numpy as np
import time

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    sys.exit(1)

from bpipe.filters import CustomFilter, PlotSink

def test_minimal_start_stop():
    """Test minimal filter start/stop to isolate threading issues."""
    print("\n=== Test 1: Minimal Filter Start/Stop ===")
    
    try:
        def dummy_transform(inputs):
            # Return minimal data
            return [np.array([1.0], dtype=np.float32)]
        
        # Create filter but don't connect it to anything
        custom_filter = CustomFilter(dummy_transform)
        print("✓ CustomFilter created")
        
        # Try to start it
        print("Starting filter...")
        custom_filter.start()
        print("✓ Filter started")
        
        # Let it run briefly
        time.sleep(0.1)
        
        # Stop it
        print("Stopping filter...")
        custom_filter.stop()
        print("✓ Filter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_aggregator_start_stop():
    """Test aggregator start/stop."""
    print("\n=== Test 2: Aggregator Start/Stop ===")
    
    try:
        # Create plot sink (which contains aggregator)
        plot_sink = PlotSink()
        print("✓ PlotSink created")
        
        # Try to start it
        print("Starting PlotSink...")
        plot_sink.start()
        print("✓ PlotSink started")
        
        # Let it run briefly
        time.sleep(0.1)
        
        # Stop it
        print("Stopping PlotSink...")
        plot_sink.stop()
        print("✓ PlotSink stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_connection_then_start():
    """Test connecting filters then starting."""
    print("\n=== Test 3: Connect Then Start ===")
    
    try:
        def dummy_transform(inputs):
            return [np.array([1.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(dummy_transform)
        plot_sink = PlotSink()
        print("✓ Filters created")
        
        # Connect them
        plot_sink.add_source(custom_filter)
        print("✓ Filters connected")
        
        # Start custom filter first
        print("Starting custom filter...")
        custom_filter.start()
        print("✓ Custom filter started")
        
        time.sleep(0.1)
        
        # Start plot sink
        print("Starting plot sink...")
        plot_sink.start()
        print("✓ Plot sink started")
        
        time.sleep(0.1)
        
        # Stop in reverse order
        print("Stopping plot sink...")
        plot_sink.stop()
        print("✓ Plot sink stopped")
        
        print("Stopping custom filter...")
        custom_filter.stop()
        print("✓ Custom filter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_base_filter_operations():
    """Test basic BpFilterBase operations."""
    print("\n=== Test 4: Base Filter Operations ===")
    
    try:
        # Create base filter directly
        base_filter = dpcore.BpFilterBase(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print("✓ BpFilterBase created")
        
        # Test start/stop
        print("Starting base filter...")
        base_filter.run()  # This is the method name for base filters
        print("✓ Base filter started")
        
        time.sleep(0.1)
        
        print("Stopping base filter...")
        base_filter.stop()
        print("✓ Base filter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_filter_py_operations():
    """Test BpFilterPy operations."""
    print("\n=== Test 5: BpFilterPy Operations ===")
    
    try:
        # Create BpFilterPy directly
        filter_py = dpcore.BpFilterPy(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print("✓ BpFilterPy created")
        
        # Test start/stop
        print("Starting BpFilterPy...")
        filter_py.run()
        print("✓ BpFilterPy started")
        
        time.sleep(0.1)
        
        print("Stopping BpFilterPy...")
        filter_py.stop()
        print("✓ BpFilterPy stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run segfault debugging tests."""
    print("Segfault Debugging Suite")
    print("=" * 30)
    
    tests = [
        test_base_filter_operations,
        test_filter_py_operations,
        test_aggregator_start_stop,
        test_minimal_start_stop,
        test_connection_then_start,
    ]
    
    results = []
    
    for i, test_func in enumerate(tests, 1):
        print(f"\nRunning test {i}/{len(tests)}: {test_func.__name__}")
        try:
            result = test_func()
            results.append((test_func.__name__, result))
            if result:
                print(f"✓ Test {i} PASSED")
            else:
                print(f"✗ Test {i} FAILED")
                break  # Stop on first failure to isolate issue
        except Exception as e:
            print(f"💥 Test {i} CRASHED: {e}")
            results.append((test_func.__name__, False))
            break
    
    print(f"\n" + "=" * 30)
    print("Debug Results:")
    for test_name, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"  {status}: {test_name}")
    
    if all(result for _, result in results):
        print("\n🎉 All tests passed - segfault might be in more complex interactions")
    else:
        print("\n🔍 Found failing test - investigate this component first")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())