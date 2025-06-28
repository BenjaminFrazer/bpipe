#!/usr/bin/env python3
"""
Debug complex scenarios that might cause segfaults.
"""

import signal
import sys
import time
import traceback


def signal_handler(sig, frame):
    print(f"\nReceived signal {sig}")
    traceback.print_stack(frame)
    sys.exit(1)

signal.signal(signal.SIGSEGV, signal_handler)

def test_custom_filter():
    """Test custom filter creation and usage."""
    print("Testing custom filter...")
    try:
        import numpy as np

        from bpipe.filters import CustomFilter

        def simple_transform(inputs):
            if not inputs or inputs[0] is None:
                return [np.array([])]
            return [inputs[0] * 2]  # Simple scaling

        custom = CustomFilter(simple_transform)
        print("✓ Created custom filter")

        # Test start/stop
        custom.start()
        print("✓ Custom filter started")

        custom.stop()
        print("✓ Custom filter stopped")

        return True
    except Exception as e:
        print(f"✗ Custom filter test failed: {e}")
        traceback.print_exc()
        return False

def test_pipeline_construction():
    """Test building a complete pipeline."""
    print("\nTesting pipeline construction...")
    try:
        import numpy as np

        from bpipe.filters import CustomFilter, FilterFactory

        # Create filters
        gen = FilterFactory.signal_generator('sawtooth', 0.01, 100.0)

        def scaling_transform(inputs):
            if not inputs or inputs[0] is None:
                return [np.array([])]
            return [inputs[0] * 2.0 + 10.0]

        scaler = CustomFilter(scaling_transform)
        passthrough = FilterFactory.passthrough()

        print("✓ Created all filters")

        # Connect pipeline
        gen.add_sink(scaler)
        scaler.add_sink(passthrough)
        print("✓ Connected pipeline")

        return True
    except Exception as e:
        print(f"✗ Pipeline construction failed: {e}")
        traceback.print_exc()
        return False

def test_sequential_start_stop():
    """Test starting and stopping filters in sequence."""
    print("\nTesting sequential start/stop...")
    try:
        import numpy as np

        from bpipe.filters import CustomFilter, FilterFactory

        # Create pipeline
        gen = FilterFactory.signal_generator('sine', 0.01, 50.0)

        def simple_transform(inputs):
            if not inputs or inputs[0] is None:
                return [np.array([])]
            return [inputs[0] * 2]

        custom = CustomFilter(simple_transform)
        passthrough = FilterFactory.passthrough()

        # Connect
        gen.add_sink(custom)
        custom.add_sink(passthrough)
        print("✓ Pipeline connected")

        # Start in reverse order (sink to source)
        print("Starting filters...")
        passthrough.start()
        print("  ✓ Passthrough started")

        custom.start()
        print("  ✓ Custom filter started")

        gen.start()
        print("  ✓ Generator started")

        # Check status
        print(f"  Generator running: {gen.running}")
        print(f"  Custom running: {custom.running}")
        print(f"  Passthrough running: {passthrough.running}")

        # Stop in forward order (source to sink)
        print("Stopping filters...")
        gen.stop()
        print("  ✓ Generator stopped")

        custom.stop()
        print("  ✓ Custom filter stopped")

        passthrough.stop()
        print("  ✓ Passthrough stopped")

        return True
    except Exception as e:
        print(f"✗ Sequential start/stop failed: {e}")
        traceback.print_exc()
        return False

def test_with_delay():
    """Test with a delay between start and stop."""
    print("\nTesting with processing delay...")
    try:
        from bpipe.filters import FilterFactory

        gen = FilterFactory.signal_generator('square', 0.01, 30.0)
        passthrough = FilterFactory.passthrough()

        gen.add_sink(passthrough)
        print("✓ Pipeline connected")

        # Start
        passthrough.start()
        gen.start()
        print("✓ Filters started")

        # Let them run briefly
        print("Processing for 1 second...")
        time.sleep(1)

        # Stop
        gen.stop()
        passthrough.stop()
        print("✓ Filters stopped")

        return True
    except Exception as e:
        print(f"✗ Delay test failed: {e}")
        traceback.print_exc()
        return False

def test_multiple_pipelines():
    """Test creating multiple independent pipelines."""
    print("\nTesting multiple pipelines...")
    try:
        from bpipe.filters import FilterFactory

        # Pipeline 1
        gen1 = FilterFactory.signal_generator('sine', 0.01, 50.0)
        pass1 = FilterFactory.passthrough()
        gen1.add_sink(pass1)

        # Pipeline 2
        gen2 = FilterFactory.signal_generator('square', 0.02, 30.0)
        pass2 = FilterFactory.passthrough()
        gen2.add_sink(pass2)

        print("✓ Created two pipelines")

        # Start both
        pass1.start()
        gen1.start()
        pass2.start()
        gen2.start()
        print("✓ Started both pipelines")

        # Brief processing
        time.sleep(0.5)

        # Stop both
        gen1.stop()
        pass1.stop()
        gen2.stop()
        pass2.stop()
        print("✓ Stopped both pipelines")

        return True
    except Exception as e:
        print(f"✗ Multiple pipelines test failed: {e}")
        traceback.print_exc()
        return False

def main():
    """Run complex debugging tests."""
    print("Complex Segmentation Fault Debugging")
    print("=" * 50)

    tests = [
        ("Custom Filter", test_custom_filter),
        ("Pipeline Construction", test_pipeline_construction),
        ("Sequential Start/Stop", test_sequential_start_stop),
        ("Processing Delay", test_with_delay),
        ("Multiple Pipelines", test_multiple_pipelines),
    ]

    passed = 0
    for name, test_func in tests:
        print(f"\n--- {name} ---")
        try:
            if test_func():
                print(f"✓ {name} passed")
                passed += 1
            else:
                print(f"✗ {name} failed")
        except Exception as e:
            print(f"✗ {name} crashed: {e}")
            traceback.print_exc()

    print(f"\n{'='*50}")
    print(f"Complex Debug Results: {passed}/{len(tests)} tests passed")

    if passed == len(tests):
        print("🎉 All complex tests passed! Segfault might be elsewhere.")
    else:
        print(f"❌ {len(tests) - passed} test(s) failed")

if __name__ == "__main__":
    main()
