#!/usr/bin/env python3
"""
Test exact CustomFilter implementation.
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

def test_exact_customfilter():
    """Test the exact CustomFilter implementation."""
    print("\n=== Test: Exact CustomFilter Implementation ===")
    
    try:
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        # Exact replication of CustomFilter.__init__
        buffer_size = 1024
        batch_size = 64
        
        if dpcore is None:
            base_filter = None
        else:
            # Create a dynamic class that inherits from BpFilterPy
            class UserFilter(dpcore.BpFilterPy):
                def __init__(self, user_func, *args, **kwargs):
                    super().__init__(*args, **kwargs)
                    self.user_func = user_func
                
                def transform(self, inputs, outputs):
                    # C code calls transform(input_list, output_list) - no timestamp
                    # Call the user transform function
                    try:
                        results = self.user_func(inputs)
                        
                        # Copy results to output arrays
                        if isinstance(results, list):
                            for i, result in enumerate(results):
                                if i < len(outputs) and result is not None and len(result) > 0:
                                    copy_len = min(len(result), len(outputs[i]))
                                    outputs[i][:copy_len] = result[:copy_len]
                        else:
                            # Single output case
                            if len(outputs) > 0 and results is not None and len(results) > 0:
                                copy_len = min(len(results), len(outputs[0]))
                                outputs[0][:copy_len] = results[:copy_len]
                    except Exception as e:
                        print(f"Error in custom transform: {e}")
            
            base_filter = UserFilter(transform_func, capacity_exp=10, dtype=2)
        
        print("✓ CustomFilter-style object created")
        
        # Test start/stop
        print("Starting filter...")
        base_filter.run()
        print("✓ Filter started")
        
        time.sleep(0.5)
        
        print("Stopping filter...")
        base_filter.stop()
        print("✓ Filter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_customfilter_import():
    """Test importing and using actual CustomFilter."""
    print("\n=== Test: Actual CustomFilter Import ===")
    
    try:
        from bpipe.filters import CustomFilter
        print("✓ CustomFilter imported")
        
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(transform_func)
        print("✓ CustomFilter created")
        
        # Don't start it, just check it exists
        print(f"  Running: {custom_filter.running}")
        print(f"  Has base filter: {custom_filter._base_filter is not None}")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_customfilter_start():
    """Test starting actual CustomFilter."""
    print("\n=== Test: CustomFilter Start ===")
    
    try:
        from bpipe.filters import CustomFilter
        
        def transform_func(inputs):
            return [np.array([1.0, 2.0, 3.0], dtype=np.float32)]
        
        custom_filter = CustomFilter(transform_func)
        print("✓ CustomFilter created")
        
        print("Starting CustomFilter...")
        custom_filter.start()
        print("✓ CustomFilter started")
        
        time.sleep(0.1)
        
        print("Stopping CustomFilter...")
        custom_filter.stop()
        print("✓ CustomFilter stopped")
        
        return True
        
    except Exception as e:
        print(f"✗ Test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """Run exact CustomFilter tests."""
    print("Exact CustomFilter Debug Tests")
    print("=" * 35)
    
    tests = [
        test_exact_customfilter,
        test_customfilter_import,
        test_customfilter_start,
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
    
    print(f"\n🎉 All tests passed!")
    return 0

if __name__ == "__main__":
    sys.exit(main())