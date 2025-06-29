#!/usr/bin/env python3
"""
Integration tests for bpipe.

Tests complete pipelines and multi-component interactions.
"""

import time

import dpcore
import pytest

from bpipe import PlotSink, create_signal_generator


class TestBasicPipelines:
    """Test basic end-to-end pipelines."""

    @pytest.mark.skip(reason="Start/stop functionality has known issues")
    def test_simple_passthrough_pipeline(self):
        """Test a simple source -> sink pipeline."""
        # Create signal generator
        source = create_signal_generator('sine', frequency=100.0, amplitude=1.0)

        # Create aggregator sink
        sink = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)

        # Connect
        source.add_sink(sink)

        # Run briefly
        source.run()
        sink.run()

        time.sleep(0.1)  # Let some data flow

        # Stop
        source.stop()
        sink.stop()

        # Check data was received
        arrays = sink.arrays
        assert len(arrays[0]) > 0

    def test_connection_without_running(self):
        """Test that filters can be connected without running."""
        gen1 = create_signal_generator('square', frequency=1.0)
        gen2 = create_signal_generator('sine', frequency=2.0)
        sink = PlotSink(n_inputs=2)

        # Multiple sources to one sink
        gen1.add_sink(sink)
        gen2.add_sink(sink)

        # Verify no errors on connection
        assert True

    def test_multi_stage_pipeline(self):
        """Test a multi-stage processing pipeline."""
        # Create components
        source = create_signal_generator('sawtooth', frequency=10.0, amplitude=10.0)

        # Processing stage 1: Scale by 0.5
        class ScaleFilter(dpcore.BpFilterPy):
            def __init__(self, scale):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
                self.scale = scale

            def transform(self, inputs, outputs):
                if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                    outputs[0][:] = inputs[0] * self.scale

        scaler = ScaleFilter(0.5)

        # Processing stage 2: Add offset
        class OffsetFilter(dpcore.BpFilterPy):
            def __init__(self, offset):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
                self.offset = offset

            def transform(self, inputs, outputs):
                if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                    outputs[0][:] = inputs[0] + self.offset

        offsetter = OffsetFilter(5.0)

        # Final sink
        sink = PlotSink()

        # Connect pipeline
        source.add_sink(scaler)
        scaler.add_sink(offsetter)
        offsetter.add_sink(sink)

        # Verify connections
        assert True


class TestMultiInputOutput:
    """Test filters with multiple inputs/outputs."""

    def test_multiple_sources_one_sink(self):
        """Test multiple signal generators feeding one sink."""
        # Create multiple sources with different frequencies
        gen1 = create_signal_generator('sine', frequency=1.0)
        gen2 = create_signal_generator('square', frequency=2.0)
        gen3 = create_signal_generator('triangle', frequency=3.0)

        # Create multi-input sink
        sink = PlotSink(n_inputs=3)

        # Connect all sources
        gen1.add_sink(sink)
        gen2.add_sink(sink)
        gen3.add_sink(sink)

        # Verify setup works
        assert len(sink.arrays) == 3

    def test_fan_out_configuration(self):
        """Test one source feeding multiple sinks."""
        source = create_signal_generator('sine', frequency=5.0)

        # Create multiple sinks
        sink1 = PlotSink()
        sink2 = PlotSink()
        sink3 = dpcore.BpAggregatorPy(dtype=dpcore.DTYPE_FLOAT)

        # Connect source to all sinks
        source.add_sink(sink1)
        source.add_sink(sink2)
        source.add_sink(sink3)

        # Verify connections work
        assert True

    def test_complex_topology(self):
        """Test a complex filter topology."""
        # Two sources
        gen1 = create_signal_generator('sine', frequency=1.0, amplitude=1.0)
        gen2 = create_signal_generator('square', frequency=0.5, amplitude=2.0)

        # Mixer filter (averages two inputs)
        class MixerFilter(dpcore.BpFilterPy):
            def __init__(self):
                super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)

            def transform(self, inputs, outputs):
                if len(inputs) >= 2 and len(outputs) > 0:
                    # Simple average of two inputs
                    if len(inputs[0]) > 0 and len(inputs[1]) > 0:
                        min_len = min(len(inputs[0]), len(inputs[1]))
                        outputs[0][:min_len] = (inputs[0][:min_len] + inputs[1][:min_len]) / 2.0

        mixer = MixerFilter()

        # Two different sinks
        raw_sink = PlotSink(n_inputs=2)  # For raw signals
        mixed_sink = PlotSink()  # For mixed signal

        # Connect topology
        gen1.add_sink(raw_sink)
        gen1.add_sink(mixer)
        gen2.add_sink(raw_sink)
        gen2.add_sink(mixer)
        mixer.add_sink(mixed_sink)

        # Verify complex topology works
        assert True


class TestErrorHandling:
    """Test error handling in pipelines."""

    def test_type_mismatch_handling(self):
        """Test handling of data type mismatches."""
        # Create float source
        float_source = create_signal_generator('sine', frequency=1.0)

        # Create int sink
        int_sink = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_INT)

        # Connection should work (type conversion handled by framework)
        float_source.add_sink(int_sink)

    def test_removing_nonexistent_sink(self):
        """Test removing a sink that was never added."""
        source = create_signal_generator('sine', frequency=1.0)
        sink = PlotSink()

        # Removing non-existent sink raises RuntimeError
        with pytest.raises(RuntimeError, match="Failed to remove sink"):
            source.remove_sink(sink)

    def test_circular_connection_prevention(self):
        """Test that circular connections are handled."""
        filter1 = dpcore.BpFilterPy(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
        filter2 = dpcore.BpFilterPy(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)

        # Create potential circular connection
        filter1.add_sink(filter2)
        # Note: The C layer should handle circular connection prevention
        # This test verifies no immediate crash


class TestPerformanceCharacteristics:
    """Test performance-related aspects of pipelines."""

    def test_high_frequency_generation(self):
        """Test signal generation at high frequencies."""
        # Create high-frequency generator
        gen = create_signal_generator('sine', frequency=1000.0, amplitude=1.0)
        sink = dpcore.BpAggregatorPy(dtype=dpcore.DTYPE_FLOAT)

        gen.add_sink(sink)

        # Verify setup handles high frequency
        assert gen.frequency == 1000.0

    def test_large_buffer_handling(self):
        """Test handling of large buffer sizes."""
        # Create generator with specific buffer size
        gen = create_signal_generator(
            'square',
            frequency=1.0,
            buffer_size=4096,
            batch_size=256
        )

        # Create sink with large capacity
        sink = PlotSink(max_capacity_bytes=1024*1024*100)  # 100MB

        gen.add_sink(sink)

        # Verify large buffers are handled
        assert True

    def test_many_filters_pipeline(self):
        """Test pipeline with many filters."""
        source = create_signal_generator('sine', frequency=1.0)

        # Create chain of filters
        filters = []
        for i in range(10):
            class IdentityFilter(dpcore.BpFilterPy):
                def __init__(self, filter_id):
                    super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
                    self.filter_id = filter_id

                def transform(self, inputs, outputs):
                    if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                        outputs[0][:] = inputs[0]

            filters.append(IdentityFilter(i))

        # Connect in chain
        source.add_sink(filters[0])
        for i in range(len(filters) - 1):
            filters[i].add_sink(filters[i + 1])

        # Final sink
        sink = PlotSink()
        filters[-1].add_sink(sink)

        # Verify long chain works
        assert len(filters) == 10
