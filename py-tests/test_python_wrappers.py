#!/usr/bin/env python3
"""
Test script for Python wrapper implementation.

Tests basic functionality of the FilterFactory and CustomFilter classes.
"""


import numpy as np
import pytest


def test_imports():
    """Test that all modules can be imported."""
    import dpcore

    from bpipe import CustomFilter as CF
    from bpipe import FilterFactory as FF
    from bpipe.filters import CustomFilter, FilterFactory

    # If we get here without exceptions, imports are successful
    assert dpcore is not None
    assert FilterFactory is not None
    assert CustomFilter is not None
    assert FF is FilterFactory
    assert CF is CustomFilter


def test_filter_creation():
    """Test creating filters with factory pattern."""
    from bpipe.filters import CustomFilter, FilterFactory

    # Test signal generator creation
    signal_gen = FilterFactory.signal_generator(
        waveform='sawtooth',
        frequency=0.01,
        amplitude=100.0
    )
    assert signal_gen.filter_type == 'signal_generator'
    assert signal_gen.config['waveform'] == 3  # 3 is the enum value for sawtooth
    assert signal_gen.config['frequency'] == 0.01
    assert signal_gen.config['amplitude'] == 100.0

    # Test passthrough creation
    passthrough = FilterFactory.passthrough()
    assert passthrough.filter_type == 'passthrough'

    # Test custom filter creation
    def simple_transform(inputs):
        if not inputs or inputs[0] is None:
            return [np.array([])]
        return [inputs[0] * 2]  # Simple scaling

    custom = CustomFilter(simple_transform)
    assert custom is not None


def test_base_filter():
    """Test basic dpcore filter functionality."""
    import dpcore

    # Create base filter
    base_filter = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
    assert base_filter is not None

    # Test Python filter
    py_filter = dpcore.BpFilterPy(capacity_exp=10, dtype=2)
    assert py_filter is not None


def test_filter_connections():
    """Test connecting filters together."""
    from bpipe.filters import FilterFactory

    # Create two filters
    gen = FilterFactory.signal_generator('sine', 0.01, 50.0)
    passthrough = FilterFactory.passthrough()

    # Test connection
    gen.add_sink(passthrough)
    # No direct way to assert connection, but no exception means success

    # Test disconnection
    gen.remove_sink(passthrough)
    # No direct way to assert disconnection, but no exception means success


def test_invalid_waveform():
    """Test handling of invalid waveform input."""
    from bpipe.filters import FilterFactory

    with pytest.raises(ValueError):
        FilterFactory.signal_generator('invalid_wave', 0.01, 50.0)


def test_invalid_filter_type():
    """Test handling of invalid filter type."""
    from bpipe.filters import BuiltinFilter

    with pytest.raises(ValueError):
        BuiltinFilter('invalid_type', {})


if __name__ == "__main__":
    # Allow running with python directly
    pytest.main([__file__, "-v"])
