#!/usr/bin/env python3
"""
Test threading operations after fixing transform signature.
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

def test_custom_filter_start():
    """Test CustomFilter start after signature fix."""
    print("\n=== Test: CustomFilter Start/Stop ===")
    
    try:
        def simple_transform(inputs):
            # Return some simple data
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(simple_transform)
        print("✓ CustomFilter created")
        
        print("Starting CustomFilter...")
        custom_filter.start()
        print("✓ CustomFilter started")
        
        time.sleep(0.5)
        
        print("Stopping CustomFilter...")
        custom_filter.stop()
        print("✓ CustomFilter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_full_pipeline():
    """Test full signal generator pipeline."""
    print("\n=== Test: Full Pipeline ===")
    
    try:
        def signal_transform(inputs):
            # Generate a small sine wave
            t = np.arange(32, dtype=np.float32)
            signal = np.sin(0.1 * t)
            return [signal]
        
        # Create pipeline
        signal_gen = CustomFilter(signal_transform)
        plot_sink = PlotSink(max_capacity_bytes=64*1024)
        
        print("✓ Filters created")
        
        # Connect them
        plot_sink.add_source(signal_gen)
        print("✓ Filters connected")
        
        # Start pipeline
        signal_gen.start()
        plot_sink.start()
        print("✓ Pipeline started")
        
        # Let it run
        time.sleep(1.0)
        
        # Check data
        arrays = plot_sink.arrays
        sizes = plot_sink.sizes
        print(f"✓ Data collected: {len(arrays)} arrays, sizes: {sizes}")
        
        # Stop pipeline
        signal_gen.stop()
        plot_sink.stop()
        print("✓ Pipeline stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run threading tests."""
    print("Threading Debug Tests")
    print("=" * 25)
    
    tests = [
        test_custom_filter_start,
        test_full_pipeline,
    ]
    
    for i, test_func in enumerate(tests, 1):
        print(f"\nTest {i}/{len(tests)}: {test_func.__name__}")
        try:
            result = test_func()
            if result:
                print(f"✓ Test {i} PASSED")
            else:
                print(f"✗ Test {i} FAILED")
                return 1
        except Exception as e:
            print(f"💥 Test {i} CRASHED: {e}")
            return 1
    
    print(f"\n🎉 All tests passed! Segfault is fixed!")
    return 0

if __name__ == "__main__":
    sys.exit(main())