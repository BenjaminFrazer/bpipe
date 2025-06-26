#!/usr/bin/env python3
"""
Simple test to isolate segfault issues.
"""

import sys
import os
import numpy as np

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    sys.exit(1)

from bpipe.filters import PlotSink, CustomFilter

def test_basic_custom_filter():
    """Test CustomFilter creation without running."""
    print("Testing CustomFilter creation...")
    
    def simple_transform(inputs):
        # Simple passthrough
        if inputs and len(inputs) > 0:
            return inputs
        else:
            # Generate some data
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
    
    try:
        custom_filter = CustomFilter(simple_transform)
        print("✓ CustomFilter created successfully")
        return True
    except Exception as e:
        print(f"✗ CustomFilter creation failed: {e}")
        return False

def test_plot_sink_creation():
    """Test PlotSink creation."""
    print("Testing PlotSink creation...")
    
    try:
        plot_sink = PlotSink()
        print("✓ PlotSink created successfully")
        print(f"  Initial arrays: {len(plot_sink.arrays)}")
        print(f"  Initial sizes: {plot_sink.sizes}")
        return True
    except Exception as e:
        print(f"✗ PlotSink creation failed: {e}")
        return False

def test_connection_without_start():
    """Test connecting filters without starting them."""
    print("Testing filter connection...")
    
    try:
        def simple_transform(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(simple_transform)
        plot_sink = PlotSink()
        
        # Try to connect them
        plot_sink.add_source(custom_filter)
        print("✓ Filters connected successfully")
        return True
    except Exception as e:
        print(f"✗ Filter connection failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    print("Simple Demo Test")
    print("=" * 20)
    
    try:
        success = True
        success &= test_basic_custom_filter()
        success &= test_plot_sink_creation()
        success &= test_connection_without_start()
        
        if success:
            print("\n✓ All basic tests passed!")
        else:
            print("\n✗ Some tests failed")
            
    except Exception as e:
        print(f"\n💥 Test failed: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())