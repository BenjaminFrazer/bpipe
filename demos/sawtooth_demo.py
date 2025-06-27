#!/usr/bin/env python3
"""
Simple sawtooth signal demonstration.

Creates a sawtooth signal generator -> passthrough filter -> plot aggregator pipeline.
Displays the plot and keeps the window open until the user closes it.
"""

import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bpipe
import matplotlib.pyplot as plt
import time


# Create passthrough filter by inheriting BpFilterPy
class Passthrough(bpipe.BpFilterPy):
    def __init__(self):
        super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
        
    def transform(self, inputs, outputs):
        if inputs and len(inputs[0]) > 0:
            outputs[0][:len(inputs[0])] = inputs[0]


def main():
    # Create pipeline components
    signal = bpipe.create_signal_generator(
        waveform='sawtooth',
        frequency=0.01,  # 1 cycle per 100 samples
        amplitude=1.0
    )
    
    passthrough = Passthrough()
    plot_sink = bpipe.PlotSink(max_points=1000)
    
    # Connect pipeline: signal -> passthrough -> plot_sink
    signal.add_sink(passthrough)
    passthrough.add_sink(plot_sink)
    
    # Start all components
    signal.run()
    passthrough.run()
    plot_sink.run()
    
    # Collect data for 2 seconds
    print("Collecting data...")
    time.sleep(2)
    
    # Stop data collection
    signal.stop()
    passthrough.stop()
    plot_sink.stop()
    
    # Create plot
    print("Displaying plot...")
    plot_sink.plot(title="Sawtooth Signal Demo")
    
    # Keep plot window open
    plt.show()


if __name__ == "__main__":
    main()