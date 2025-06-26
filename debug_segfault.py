#!/usr/bin/env python3
"""
Minimal reproduction case for segmentation fault debugging.
"""

import traceback
import signal
import sys

def signal_handler(sig, frame):
    print(f"\nReceived signal {sig}")
    traceback.print_stack(frame)
    sys.exit(1)

signal.signal(signal.SIGSEGV, signal_handler)

def test_basic_creation():
    """Test basic filter creation without start/stop."""
    print("Testing basic filter creation...")
    try:
        from bpipe.filters import FilterFactory
        
        # Create filters
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        passthrough = FilterFactory.passthrough()
        
        print(f"✓ Created signal generator: {gen.filter_type}")
        print(f"✓ Created passthrough: {passthrough.filter_type}")
        
        # Test connection
        gen.add_sink(passthrough)
        print("✓ Connected filters")
        
        return True
    except Exception as e:
        print(f"✗ Basic creation failed: {e}")
        traceback.print_exc()
        return False

def test_start_stop_individually():
    """Test start/stop on individual filters."""
    print("\nTesting individual start/stop...")
    try:
        from bpipe.filters import FilterFactory
        
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        print("✓ Created signal generator")
        
        # Test just the start operation
        print("Attempting to start filter...")
        gen.start()
        print("✓ Filter started successfully")
        
        # Test just the stop operation  
        print("Attempting to stop filter...")
        gen.stop()
        print("✓ Filter stopped successfully")
        
        return True
    except Exception as e:
        print(f"✗ Start/stop failed: {e}")
        traceback.print_exc()
        return False

def test_base_filter_directly():
    """Test dpcore base filter directly."""
    print("\nTesting dpcore directly...")
    try:
        import dpcore
        
        # Test base filter creation
        base = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        print("✓ Created base filter")
        
        # Test Python filter creation
        py_filter = dpcore.BpFilterPy(capacity_exp=10, dtype=2)
        print("✓ Created Python filter")
        
        # Test connection
        base.add_sink(py_filter)
        print("✓ Connected filters")
        
        return True
    except Exception as e:
        print(f"✗ Direct dpcore test failed: {e}")
        traceback.print_exc()
        return False

def test_base_filter_start_stop():
    """Test start/stop on base dpcore filters."""
    print("\nTesting dpcore start/stop...")
    try:
        import dpcore
        
        base = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        print("✓ Created base filter")
        
        print("Attempting to start base filter...")
        base.run()  # Note: method is called 'run' not 'start'
        print("✓ Base filter started")
        
        print("Attempting to stop base filter...")
        base.stop()
        print("✓ Base filter stopped")
        
        return True
    except Exception as e:
        print(f"✗ Base filter start/stop failed: {e}")
        traceback.print_exc()
        return False

def main():
    """Run debugging tests in order of complexity."""
    print("Segmentation Fault Debugging")
    print("=" * 40)
    
    tests = [
        ("Basic Creation", test_basic_creation),
        ("Direct dpcore", test_base_filter_directly),
        ("Base Filter Start/Stop", test_base_filter_start_stop),
        ("Wrapper Start/Stop", test_start_stop_individually),
    ]
    
    for name, test_func in tests:
        print(f"\n--- {name} ---")
        try:
            if test_func():
                print(f"✓ {name} passed")
            else:
                print(f"✗ {name} failed")
                break  # Stop at first failure to isolate issue
        except Exception as e:
            print(f"✗ {name} crashed: {e}")
            traceback.print_exc()
            break
    
    print(f"\n{'='*40}")
    print("Debug session complete")

if __name__ == "__main__":
    main()