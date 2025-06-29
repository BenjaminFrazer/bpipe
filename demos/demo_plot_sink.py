#!/usr/bin/env python3
"""
Demo script showing PlotSink functionality.

Demonstrates basic PlotSink usage patterns.
"""

import time
import matplotlib.pyplot as plt
import bpipe


def demo_basic_plotting():
    """Demonstrate basic PlotSink functionality."""
    print("Creating PlotSink...")
    sink = bpipe.PlotSink(max_points=1000)
    print(f"PlotSink created with max_points={sink.max_points}")

    # Create signal generator for data
    gen = bpipe.create_signal_generator('sine', frequency=0.05, amplitude=1.0)
    
    print("Connecting generator to sink...")
    gen.add_sink(sink)
    
    print("Starting pipeline...")
    gen.run()
    sink.run()
    
    print("Collecting data...")
    time.sleep(1.0)
    
    print("Stopping pipeline...")
    gen.stop()
    sink.stop()
    
    print("Creating plot...")
    fig = sink.plot(title="PlotSink Demo")
    fig.savefig('plot_sink_demo.png', dpi=150, bbox_inches='tight')
    print("Plot saved to plot_sink_demo.png")
    
    return fig


def demo_multi_input():
    """Demonstrate PlotSink with multiple inputs."""
    print("\nMulti-input PlotSink demo...")
    
    sink = bpipe.PlotSink(max_points=500)
    
    # Create multiple signal generators
    gen1 = bpipe.create_signal_generator('sine', frequency=0.02, amplitude=1.0)
    gen2 = bpipe.create_signal_generator('square', frequency=0.03, amplitude=0.8)
    
    gen1.add_sink(sink)
    gen2.add_sink(sink)
    
    gen1.run()
    gen2.run()
    sink.run()
    
    time.sleep(1.5)
    
    gen1.stop()
    gen2.stop()
    sink.stop()
    
    fig = sink.plot(title="Multi-Input PlotSink Demo")
    fig.savefig('multi_input_demo.png', dpi=150, bbox_inches='tight')
    print("Multi-input plot saved to multi_input_demo.png")
    
    return fig


def main():
    """Main demo function."""
    print("PlotSink Integration Demo")
    print("=" * 40)

    demo_basic_plotting()
    demo_multi_input()
    
    print("\nPlotSink demos completed successfully!")
    print("Generated plots:")
    print("   - plot_sink_demo.png: Basic PlotSink usage")
    print("   - multi_input_demo.png: Multiple input demonstration")


if __name__ == "__main__":
    main()
