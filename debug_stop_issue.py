#!/usr/bin/env python3
"""
Debug the specific stop issue.
"""

import traceback
import signal
import sys

def signal_handler(sig, frame):
    print(f"\nReceived signal {sig}")
    traceback.print_stack(frame)
    sys.exit(1)

signal.signal(signal.SIGSEGV, signal_handler)

def test_single_filter_stop():
    """Test stopping a single filter."""
    print("Testing single filter stop...")
    try:
        from bpipe.filters import FilterFactory
        
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        
        gen.start()
        print("✓ Single filter started")
        
        gen.stop()
        print("✓ Single filter stopped")
        
        return True
    except Exception as e:
        print(f"✗ Single filter stop failed: {e}")
        traceback.print_exc()
        return False

def test_connected_but_stop_individually():
    """Test connected filters but stop them individually."""
    print("\nTesting connected filters with individual stops...")
    try:
        from bpipe.filters import FilterFactory
        
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        passthrough = FilterFactory.passthrough()
        
        gen.add_sink(passthrough)
        print("✓ Filters connected")
        
        # Start both
        passthrough.start()
        gen.start()
        print("✓ Both filters started")
        
        # Stop them one by one with delay
        print("Stopping generator...")
        gen.stop()
        print("✓ Generator stopped")
        
        print("Stopping passthrough...")
        passthrough.stop()
        print("✓ Passthrough stopped")
        
        return True
    except Exception as e:
        print(f"✗ Individual stop test failed: {e}")
        traceback.print_exc()
        return False

def test_direct_dpcore_stop():
    """Test dpcore filters directly."""
    print("\nTesting direct dpcore stop...")
    try:
        import dpcore
        
        filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        filter2 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
        
        filter1.add_sink(filter2)
        print("✓ dpcore filters connected")
        
        # Start
        filter2.run()
        filter1.run()
        print("✓ dpcore filters started")
        
        # Stop
        print("Stopping filter1...")
        filter1.stop()
        print("✓ filter1 stopped")
        
        print("Stopping filter2...")
        filter2.stop()
        print("✓ filter2 stopped")
        
        return True
    except Exception as e:
        print(f"✗ Direct dpcore stop failed: {e}")
        traceback.print_exc()
        return False

def test_no_connection_stop():
    """Test starting/stopping without connections."""
    print("\nTesting unconnected filters...")
    try:
        from bpipe.filters import FilterFactory
        
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
        passthrough = FilterFactory.passthrough()
        
        # Start both without connecting
        gen.start()
        passthrough.start()
        print("✓ Unconnected filters started")
        
        # Stop both
        gen.stop()
        passthrough.stop()
        print("✓ Unconnected filters stopped")
        
        return True
    except Exception as e:
        print(f"✗ Unconnected filter test failed: {e}")
        traceback.print_exc()
        return False

def main():
    """Run stop-specific debugging tests."""
    print("Stop Issue Debugging")
    print("=" * 30)
    
    tests = [
        ("Single Filter Stop", test_single_filter_stop),
        ("Direct dpcore Stop", test_direct_dpcore_stop),
        ("Unconnected Filters", test_no_connection_stop),
        ("Connected Individual Stops", test_connected_but_stop_individually),
    ]
    
    for name, test_func in tests:
        print(f"\n--- {name} ---")
        try:
            if test_func():
                print(f"✓ {name} passed")
            else:
                print(f"✗ {name} failed")
                break  # Stop at first failure
        except Exception as e:
            print(f"✗ {name} crashed: {e}")
            traceback.print_exc()
            break
    
    print(f"\n{'='*30}")
    print("Stop debugging complete")

if __name__ == "__main__":
    main()