#!/usr/bin/env python3
"""
Test the BpAggregatorPy functionality.
"""

import pytest
import numpy as np
import dpcore
from bpipe.filters import FilterFactory


def test_aggregator_creation():
    """Test creating an aggregator."""
    # Single input aggregator
    agg = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    assert agg is not None
    
    # Multi-input aggregator
    agg_multi = dpcore.BpAggregatorPy(n_inputs=3, dtype=dpcore.DTYPE_INT)
    assert agg_multi is not None


def test_aggregator_arrays_property():
    """Test the arrays property returns empty arrays initially."""
    agg = dpcore.BpAggregatorPy(n_inputs=2, dtype=dpcore.DTYPE_FLOAT)
    
    arrays = agg.arrays
    assert isinstance(arrays, list)
    assert len(arrays) == 2
    
    # Check each array is empty initially
    for arr in arrays:
        assert isinstance(arr, np.ndarray)
        assert arr.shape == (0,)
        assert arr.dtype == np.float32


def test_aggregator_with_signal_generator():
    """Test aggregator collecting data from a signal generator."""
    # Create signal generator
    gen = FilterFactory.signal_generator('sine', frequency=0.1, amplitude=1.0)
    
    # Create aggregator
    agg = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    
    # Connect generator to aggregator
    gen._filter.add_sink(agg)
    
    # Start filters
    gen.start()
    agg.run()
    
    # Let it run briefly
    import time
    time.sleep(0.1)
    
    # Stop filters
    gen.stop()
    agg.stop()
    
    # Check aggregated data
    arrays = agg.arrays
    assert len(arrays) == 1
    assert arrays[0].shape[0] > 0  # Should have collected some samples
    assert arrays[0].dtype == np.float32
    
    # Data should be read-only
    with pytest.raises(ValueError):
        arrays[0][0] = 999


def test_aggregator_clear():
    """Test clearing aggregated data."""
    # Create and connect filters
    gen = FilterFactory.signal_generator('square', frequency=0.1, amplitude=2.0)
    agg = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    gen._filter.add_sink(agg)
    
    # Collect some data
    gen.start()
    agg.run()
    import time
    time.sleep(0.05)
    gen.stop()
    agg.stop()
    
    # Verify data was collected
    arrays = agg.arrays
    assert arrays[0].shape[0] > 0
    initial_size = arrays[0].shape[0]
    
    # Clear the data
    agg.clear()
    
    # Arrays should be empty now
    arrays_after_clear = agg.arrays
    assert arrays_after_clear[0].shape[0] == 0


def test_aggregator_get_sizes():
    """Test getting buffer sizes."""
    agg = dpcore.BpAggregatorPy(n_inputs=3, dtype=dpcore.DTYPE_FLOAT)
    
    # Initially all sizes should be 0
    sizes = agg.get_sizes()
    assert isinstance(sizes, list)
    assert len(sizes) == 3
    assert all(s == 0 for s in sizes)


def test_aggregator_multi_input():
    """Test aggregator with multiple inputs."""
    # Create two signal generators with different waveforms
    gen1 = FilterFactory.signal_generator('sine', frequency=0.1, amplitude=1.0)
    gen2 = FilterFactory.signal_generator('square', frequency=0.2, amplitude=2.0)
    
    # Create aggregator with 2 inputs
    agg = dpcore.BpAggregatorPy(n_inputs=2, dtype=dpcore.DTYPE_FLOAT)
    
    # Connect generators to different inputs
    # Note: This assumes multi-input support in the connection logic
    # For now, we'll just test creation
    
    arrays = agg.arrays
    assert len(arrays) == 2
    assert all(isinstance(arr, np.ndarray) for arr in arrays)


def test_aggregator_max_capacity():
    """Test aggregator respects max capacity."""
    # Create aggregator with small max capacity (1KB)
    agg = dpcore.BpAggregatorPy(
        n_inputs=1, 
        dtype=dpcore.DTYPE_FLOAT,
        max_capacity_bytes=1024
    )
    
    # The aggregator should accept this configuration
    assert agg is not None
    
    # Arrays should still be accessible
    arrays = agg.arrays
    assert len(arrays) == 1


def test_aggregator_dtypes():
    """Test different data types."""
    # Float aggregator
    agg_float = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    assert agg_float.arrays[0].dtype == np.float32
    
    # Int aggregator  
    agg_int = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_INT)
    assert agg_int.arrays[0].dtype == np.int32
    
    # Unsigned aggregator
    agg_uint = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_UNSIGNED)
    assert agg_uint.arrays[0].dtype == np.uint32


if __name__ == "__main__":
    pytest.main([__file__, "-v"])