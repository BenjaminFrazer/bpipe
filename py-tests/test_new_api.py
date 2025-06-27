#!/usr/bin/env python3
"""Test the new simplified API"""

import pytest
import sys
import os
import numpy as np

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bpipe


def test_direct_filter_creation():
    """Test creating filters directly from C extension classes"""
    
    # Create a custom filter by inheriting BpFilterPy
    class TestFilter(bpipe.BpFilterPy):
        def __init__(self):
            super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
            self.transform_called = False
            
        def transform(self, inputs, outputs):
            self.transform_called = True
            if inputs and len(inputs[0]) > 0:
                outputs[0][:len(inputs[0])] = inputs[0] * 2
    
    filter1 = TestFilter()
    assert filter1 is not None
    assert hasattr(filter1, 'run')
    assert hasattr(filter1, 'stop')
    assert hasattr(filter1, 'add_sink')


def test_signal_generator_factory():
    """Test signal generator factory function"""
    
    signal = bpipe.create_signal_generator(
        waveform='sine',
        frequency=0.1,
        amplitude=1.0
    )
    
    assert signal is not None
    assert hasattr(signal, 'run')
    assert hasattr(signal, 'stop')


def test_plot_sink_creation():
    """Test PlotSink as direct subclass"""
    
    plot = bpipe.PlotSink(max_points=100)
    assert plot is not None
    assert hasattr(plot, 'run')
    assert hasattr(plot, 'stop')
    assert hasattr(plot, 'plot')
    assert hasattr(plot, 'arrays')


def test_aggregator_direct():
    """Test using BpAggregatorPy directly"""
    
    agg = bpipe.BpAggregatorPy()
    assert agg is not None
    assert hasattr(agg, 'run')
    assert hasattr(agg, 'stop')
    assert hasattr(agg, 'arrays')


def test_filter_connection():
    """Test connecting filters together"""
    
    class Source(bpipe.BpFilterPy):
        def __init__(self):
            super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
            
        def transform(self, inputs, outputs):
            # Generate some data
            outputs[0][:10] = np.ones(10, dtype=np.float32)
    
    class Sink(bpipe.BpFilterPy):
        def __init__(self):
            super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
            
        def transform(self, inputs, outputs):
            pass
    
    source = Source()
    sink = Sink()
    
    # Should be able to connect them
    source.add_sink(sink)
    
    # Should be able to disconnect
    source.remove_sink(sink)


def test_constants_available():
    """Test that constants are exposed"""
    
    # Wave constants
    assert hasattr(bpipe, 'BP_WAVE_SINE')
    assert hasattr(bpipe, 'BP_WAVE_SQUARE')
    assert hasattr(bpipe, 'BP_WAVE_TRIANGLE')
    assert hasattr(bpipe, 'BP_WAVE_SAWTOOTH')
    
    # Data type constants
    assert hasattr(bpipe, 'DTYPE_FLOAT')
    assert hasattr(bpipe, 'DTYPE_INT')
    assert hasattr(bpipe, 'DTYPE_UNSIGNED')


if __name__ == "__main__":
    pytest.main([__file__, "-v"])