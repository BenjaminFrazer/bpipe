#!/usr/bin/env python3
"""
Test UserFilter creation specifically.
"""

import os
import sys

import numpy as np

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    sys.exit(1)

def test_bpfilterpy_creation():
    """Test BpFilterPy creation directly."""
    print("\n=== Test: Direct BpFilterPy Creation ===")

    try:
        filter_py = dpcore.BpFilterPy(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print("✓ BpFilterPy created directly")
        return True
    except Exception as e:
        print(f"✗ BpFilterPy creation failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_userfilter_creation():
    """Test UserFilter dynamic class creation."""
    print("\n=== Test: UserFilter Dynamic Class Creation ===")

    try:
        def dummy_transform(inputs):
            return [np.array([1.0], dtype=np.float32)]

        # Replicate the exact code from CustomFilter
        class UserFilter(dpcore.BpFilterPy):
            def __init__(self, user_func, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.user_func = user_func

            def transform(self, inputs, outputs):
                # C code calls transform(input_list, output_list) - no timestamp
                try:
                    results = self.user_func(inputs)
                    print(f"Transform called with {len(inputs)} inputs, {len(outputs)} outputs")
                    print(f"User function returned: {type(results)}")
                except Exception as e:
                    print(f"Error in custom transform: {e}")

        print("✓ UserFilter class defined")

        # Try to create instance
        user_filter = UserFilter(dummy_transform, capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print("✓ UserFilter instance created")

        return True

    except Exception as e:
        print(f"✗ UserFilter creation failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_userfilter_start():
    """Test starting UserFilter."""
    print("\n=== Test: UserFilter Start ===")

    try:
        def dummy_transform(inputs):
            return [np.array([1.0], dtype=np.float32)]

        class UserFilter(dpcore.BpFilterPy):
            def __init__(self, user_func, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.user_func = user_func

            def transform(self, inputs, outputs):
                try:
                    results = self.user_func(inputs)
                    print("Transform called!")
                except Exception as e:
                    print(f"Error in transform: {e}")

        user_filter = UserFilter(dummy_transform, capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        print("✓ UserFilter created")

        print("Starting UserFilter...")
        user_filter.run()
        print("✓ UserFilter started")

        import time
        time.sleep(0.1)

        print("Stopping UserFilter...")
        user_filter.stop()
        print("✓ UserFilter stopped")

        return True

    except Exception as e:
        print(f"✗ UserFilter start failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run UserFilter debug tests."""
    print("UserFilter Debug Tests")
    print("=" * 25)

    tests = [
        test_bpfilterpy_creation,
        test_userfilter_creation,
        test_userfilter_start,
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

    print("\n🎉 All UserFilter tests passed!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
