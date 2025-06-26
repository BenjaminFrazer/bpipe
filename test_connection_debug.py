#!/usr/bin/env python3
"""
Test connection logic between CustomFilter and PlotSink.
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

def test_connection_logic():
    """Test the connection between CustomFilter and PlotSink."""
    print("\n=== Test: Connection Logic ===")
    
    try:
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(transform_func)
        plot_sink = PlotSink()
        
        print("✓ Filters created")
        print(f"  CustomFilter base: {type(custom_filter._base_filter)}")
        print(f"  PlotSink aggregator: {type(plot_sink._aggregator)}")
        
        # Test the add_source method
        print("Connecting filters...")
        plot_sink.add_source(custom_filter)
        print("✓ Filters connected")
        
        return True
        
    except Exception as e:
        print(f"✗ Connection failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_manual_connection():
    """Test manual connection using base filter methods."""
    print("\n=== Test: Manual Connection ===")
    
    try:
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(transform_func)
        plot_sink = PlotSink()
        
        print("✓ Filters created")
        
        # Try manual connection
        source_base = custom_filter._get_base_filter()
        sink_base = plot_sink._aggregator
        
        print(f"Source base: {type(source_base)}")
        print(f"Sink base: {type(sink_base)}")
        
        print("Adding sink manually...")
        source_base.add_sink(sink_base)
        print("✓ Manual connection successful")
        
        return True
        
    except Exception as e:
        print(f"✗ Manual connection failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_connected_start():
    """Test starting connected filters."""
    print("\n=== Test: Connected Start ===")
    
    try:
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(transform_func)
        plot_sink = PlotSink()
        
        # Connect manually to avoid PlotSink.add_source issues
        source_base = custom_filter._get_base_filter()
        sink_base = plot_sink._aggregator
        source_base.add_sink(sink_base)
        
        print("✓ Filters connected manually")
        
        # Start source first
        print("Starting source...")
        custom_filter.start()
        print("✓ Source started")
        
        time.sleep(0.1)
        
        # Start sink
        print("Starting sink...")
        plot_sink.start()
        print("✓ Sink started")
        
        time.sleep(0.5)
        
        # Check data
        arrays = plot_sink.arrays
        sizes = plot_sink.sizes
        print(f"Data collected: {len(arrays)} arrays, sizes: {sizes}")
        
        # Stop
        plot_sink.stop()
        custom_filter.stop()
        print("✓ Stopped successfully")
        
        return True
        
    except Exception as e:
        print(f"✗ Connected start failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run connection debug tests."""
    print("Connection Debug Tests")
    print("=" * 25)
    
    tests = [
        test_connection_logic,
        test_manual_connection,
        test_connected_start,
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
    
    print(f"\n🎉 All connection tests passed!")
    return 0

if __name__ == "__main__":
    sys.exit(main())