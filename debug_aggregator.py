#!/usr/bin/env python3
"""
Debug script to isolate the aggregator segfault.
"""

import dpcore
import sys

def test_step_by_step():
    print("Step 1: Import dpcore")
    print("dpcore imported successfully")
    
    print("Step 2: Check dtype constants")
    print(f"DTYPE_FLOAT = {dpcore.DTYPE_FLOAT}")
    print(f"DTYPE_INT = {dpcore.DTYPE_INT}")
    print(f"DTYPE_UNSIGNED = {dpcore.DTYPE_UNSIGNED}")
    
    print("Step 3: Create aggregator")
    agg = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    print(f"Aggregator created: {agg}")
    
    print("Step 4: Check aggregator attributes")
    print(f"Type: {type(agg)}")
    
    print("Step 5: Access methods")
    sizes = agg.get_sizes()
    print(f"Buffer sizes: {sizes}")
    
    print("Step 6: Access arrays property (this is where segfault occurs)")
    arrays = agg.arrays
    print(f"Arrays: {arrays}")

if __name__ == "__main__":
    try:
        test_step_by_step()
        print("SUCCESS: All steps completed")
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)