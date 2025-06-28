#!/usr/bin/env python3
"""
Tests for Python filter components in bpipe.filters module.

Tests PlotSink, create_signal_generator, and custom filter creation.
"""

import pytest
import numpy as np
import dpcore
from bpipe import PlotSink, create_signal_generator


class TestSignalGenerator:
    """Test signal generator creation function."""
    
    def test_create_signal_generator_basic(self):
        """Test basic signal generator creation."""
        gen = create_signal_generator('sine', frequency=1.0)
        assert gen is not None
        assert isinstance(gen, dpcore.BpFilterPy)
        assert hasattr(gen, 'waveform')
        assert hasattr(gen, 'frequency')
        assert gen.waveform == dpcore.BP_WAVE_SINE
        assert gen.frequency == 1.0
    
    def test_signal_generator_waveforms(self):
        """Test all waveform types."""
        waveforms = [
            ('square', dpcore.BP_WAVE_SQUARE),
            ('sine', dpcore.BP_WAVE_SINE),
            ('triangle', dpcore.BP_WAVE_TRIANGLE),
            ('sawtooth', dpcore.BP_WAVE_SAWTOOTH)
        ]
        
        for waveform_str, expected_val in waveforms:
            gen = create_signal_generator(waveform_str, frequency=1.0)
            assert gen.waveform == expected_val
    
    def test_signal_generator_with_params(self):
        """Test signal generator with all parameters."""
        gen = create_signal_generator(
            waveform='square',
            frequency=10.0,
            amplitude=2.0,
            phase=0.5,
            x_offset=1.0,
            buffer_size=2048,
            batch_size=128
        )
        assert gen.frequency == 10.0
        assert gen.amplitude == 2.0
        assert gen.phase == 0.5
        assert gen.x_offset == 1.0
    
    def test_signal_generator_integer_waveform(self):
        """Test signal generator with integer waveform value."""
        gen = create_signal_generator(dpcore.BP_WAVE_TRIANGLE, frequency=1.0)
        assert gen.waveform == dpcore.BP_WAVE_TRIANGLE
    
    def test_invalid_waveform_string(self):
        """Test error handling for invalid waveform string."""
        with pytest.raises(ValueError, match="Unknown waveform"):
            create_signal_generator('invalid_waveform', frequency=1.0)


class TestPlotSink:
    """Test PlotSink functionality."""
    
    def test_plotsink_creation(self):
        """Test basic PlotSink creation."""
        sink = PlotSink()
        assert sink is not None
        assert isinstance(sink, dpcore.BpAggregatorPy)
        assert hasattr(sink, 'max_points')
        assert sink.max_points == 10000
    
    def test_plotsink_custom_params(self):
        """Test PlotSink with custom parameters."""
        sink = PlotSink(
            max_capacity_bytes=1024*1024*10,  # 10MB
            max_points=5000,
            n_inputs=3,
            dtype=dpcore.DTYPE_INT
        )
        assert sink.max_points == 5000
        
        # Check inherited aggregator properties
        arrays = sink.arrays
        assert len(arrays) == 3
        for arr in arrays:
            assert arr.dtype == np.int32
    
    def test_plotsink_arrays_property(self):
        """Test PlotSink inherits arrays property."""
        sink = PlotSink(n_inputs=2, dtype=dpcore.DTYPE_FLOAT)
        arrays = sink.arrays
        assert isinstance(arrays, list)
        assert len(arrays) == 2
        assert all(isinstance(arr, np.ndarray) for arr in arrays)
    
    def test_plotsink_methods(self):
        """Test PlotSink has expected methods."""
        sink = PlotSink()
        assert hasattr(sink, 'plot')
        assert hasattr(sink, 'clear')
        assert hasattr(sink, 'get_sizes')
        assert hasattr(sink, 'arrays')
    
    @pytest.mark.skipif(True, reason="Requires matplotlib and data to test plotting")
    def test_plotsink_plot_method(self):
        """Test PlotSink plot method (requires matplotlib)."""
        # This test would require:
        # 1. matplotlib to be installed
        # 2. Actual data in the sink
        # 3. A way to verify the plot was created correctly
        pass
    
    def test_plotsink_no_data_error(self):
        """Test PlotSink plot raises error with no data."""
        sink = PlotSink()
        with pytest.raises(RuntimeError, match="No data available"):
            sink.plot()


class TestCustomFilterCreation:
    """Test creating custom filters with BpFilterPy."""
    
    def test_custom_filter_basic(self):
        """Test creating a basic custom filter."""
        class PassthroughFilter(dpcore.BpFilterPy):
            def __init__(self):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
            
            def transform(self, inputs, outputs):
                if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                    outputs[0][:] = inputs[0]
        
        filter_obj = PassthroughFilter()
        assert isinstance(filter_obj, dpcore.BpFilterPy)
        assert hasattr(filter_obj, 'transform')
    
    def test_custom_filter_with_state(self):
        """Test custom filter with internal state."""
        class ScalingFilter(dpcore.BpFilterPy):
            def __init__(self, scale_factor=2.0):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
                self.scale_factor = scale_factor
                self.samples_processed = 0
            
            def transform(self, inputs, outputs):
                if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                    input_data = inputs[0]
                    output_data = input_data * self.scale_factor
                    outputs[0][:len(output_data)] = output_data
                    self.samples_processed += len(input_data)
        
        filter_obj = ScalingFilter(scale_factor=3.0)
        assert filter_obj.scale_factor == 3.0
        assert filter_obj.samples_processed == 0
    
    def test_generator_transform_method(self):
        """Test the transform method of signal generator."""
        gen = create_signal_generator('sine', frequency=1.0, amplitude=1.0)
        
        # Simulate transform call
        outputs = [np.zeros(64, dtype=np.float32)]
        gen.transform([], outputs)
        
        # Check that some data was generated
        assert not np.all(outputs[0] == 0)
        # Check it's within expected amplitude range
        assert np.all(np.abs(outputs[0]) <= 1.0)


class TestFilterIntegration:
    """Test integration between different filter types."""
    
    def test_signal_to_plotsink_connection(self):
        """Test connecting signal generator to plot sink."""
        gen = create_signal_generator('sine', frequency=1.0)
        sink = PlotSink()
        
        # Should connect without error
        gen.add_sink(sink)
        
        # Disconnect
        gen.remove_sink(sink)
    
    def test_custom_filter_pipeline(self):
        """Test creating a pipeline with custom filters."""
        # Signal generator
        gen = create_signal_generator('square', frequency=0.1, amplitude=5.0)
        
        # Custom processing filter
        class AbsFilter(dpcore.BpFilterPy):
            def __init__(self):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
            
            def transform(self, inputs, outputs):
                if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                    outputs[0][:] = np.abs(inputs[0])
        
        abs_filter = AbsFilter()
        sink = PlotSink()
        
        # Connect pipeline
        gen.add_sink(abs_filter)
        abs_filter.add_sink(sink)
        
        # Verify connections work (no exceptions)
        assert True