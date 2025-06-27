#!/usr/bin/env python3
"""
Example usage of bpipe Python API with direct C extension classes.

This example demonstrates:
1. Creating signal generator filters using factory function
2. Creating custom filters by inheriting from BpFilterPy
3. Connecting filters together in a processing pipeline
4. Starting/stopping filter execution
"""

import os
import sys
import time

# Add parent directory to path to import dpcore and bpipe
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bpipe


def main():
    print("bpipe Python API Example")
    print("=" * 30)

    # Example 1: Signal Generator using factory function
    print("\n1. Creating signal generator...")

    # Create a sawtooth wave generator
    signal_gen = bpipe.create_signal_generator(
        waveform='sawtooth',
        frequency=0.01,  # 1 cycle per 100 samples
        amplitude=100.0,
        phase=0.0,
        x_offset=50.0
    )

    print("   Signal generator created")

    # Example 2: Custom Filter by inheriting BpFilterPy
    print("\n2. Creating custom filter...")

    class ScalingFilter(bpipe.BpFilterPy):
        """Custom filter that scales and adds offset to input."""

        def __init__(self, scale=2.0, offset=10.0):
            super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
            self.scale = scale
            self.offset = offset

        def transform(self, inputs, outputs):
            """Transform function called by C worker thread."""
            if inputs and len(inputs[0]) > 0:
                # Scale and offset the input
                outputs[0][:len(inputs[0])] = inputs[0] * self.scale + self.offset

    scale_filter = ScalingFilter(scale=2.0, offset=10.0)
    print("   Custom scaling filter created")

    # Example 3: Passthrough filter
    print("\n3. Creating passthrough filter...")

    class PassthroughFilter(bpipe.BpFilterPy):
        """Simple passthrough filter."""

        def __init__(self):
            super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)

        def transform(self, inputs, outputs):
            if inputs and len(inputs[0]) > 0:
                outputs[0][:len(inputs[0])] = inputs[0]

    passthrough = PassthroughFilter()
    print("   Passthrough filter created")

    # Example 4: Connect filters in a pipeline
    print("\n4. Connecting filters...")
    print("   Signal Generator -> Scaling Filter -> Passthrough")

    signal_gen.add_sink(scale_filter)
    scale_filter.add_sink(passthrough)

    # Example 5: Data aggregation with PlotSink
    print("\n5. Creating PlotSink for visualization...")

    plot_sink = bpipe.PlotSink(max_points=1000)
    passthrough.add_sink(plot_sink)

    print("   Complete pipeline: Signal -> Scale -> Passthrough -> PlotSink")

    # Example 6: Start the pipeline
    print("\n6. Starting pipeline...")

    # Start all filters (order doesn't matter due to threading)
    signal_gen.run()
    scale_filter.run()
    passthrough.run()
    plot_sink.run()

    print("   All filters running")

    # Let it run for a bit
    print("\n7. Collecting data for 2 seconds...")
    time.sleep(2)

    # Example 7: Stop the pipeline
    print("\n8. Stopping pipeline...")

    signal_gen.stop()
    scale_filter.stop()
    passthrough.stop()
    plot_sink.stop()

    print("   All filters stopped")

    # Example 8: Check collected data
    print("\n9. Checking collected data...")
    sizes = plot_sink.get_sizes()
    print(f"   Collected {sizes[0] if sizes else 0} samples")

    # Example 9: Multiple sinks demonstration
    print("\n10. Demonstrating multiple sinks...")

    # Create another aggregator
    aggregator2 = bpipe.BpAggregatorPy()

    # A filter can have multiple sinks
    scale_filter.add_sink(aggregator2)

    print("   Scale filter now has 2 sinks: passthrough and aggregator2")

    # Run again briefly
    signal_gen.run()
    scale_filter.run()
    passthrough.run()
    plot_sink.run()
    aggregator2.run()

    time.sleep(1)

    # Stop all
    signal_gen.stop()
    scale_filter.stop()
    passthrough.stop()
    plot_sink.stop()
    aggregator2.stop()

    # Both should have data
    sizes1 = plot_sink.get_sizes()
    sizes2 = aggregator2.get_sizes()
    print(f"   PlotSink collected: {sizes1[0] if sizes1 else 0} samples")
    print(f"   Aggregator2 collected: {sizes2[0] if sizes2 else 0} samples")

    print("\n✓ Example completed successfully!")


if __name__ == "__main__":
    main()
