#!/usr/bin/env python3
"""Test script to verify that the Python filters work correctly with threading."""

import dpcore
import time
import threading

def test_threading():
    """Test that the filter_mutex is properly initialized and threading works."""
    print("Testing threading functionality...")
    
    # Create filters
    source = dpcore.BpFilterPy(10, dtype=dpcore.DTYPE_FLOAT)
    sink = dpcore.BpFilterPy(10, dtype=dpcore.DTYPE_FLOAT)
    
    # Connect them
    source.add_sink(sink)
    print("✓ Filters connected successfully")
    
    # Test starting the source filter
    source.run()
    print("✓ Source filter started successfully (worker thread created)")
    
    # Let it run briefly
    time.sleep(0.1)
    
    # Stop the filter
    source.stop()
    print("✓ Source filter stopped successfully (worker thread joined)")
    
    print("Threading test passed!")

def test_multiple_connections():
    """Test multiple sink connections."""
    print("Testing multiple connections...")
    
    source = dpcore.BpFilterPy(8, dtype=dpcore.DTYPE_FLOAT)
    sink1 = dpcore.BpFilterPy(8, dtype=dpcore.DTYPE_FLOAT)
    sink2 = dpcore.BpFilterPy(8, dtype=dpcore.DTYPE_FLOAT)
    
    # Connect multiple sinks
    source.add_sink(sink1)
    source.add_sink(sink2)
    print("✓ Multiple sinks connected successfully")
    
    # Test removal
    source.remove_sink(sink1)
    print("✓ Sink removal works")
    
    print("Multiple connections test passed!")

if __name__ == "__main__":
    test_threading()
    test_multiple_connections()
    print("All threading tests passed!")