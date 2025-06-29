#!/usr/bin/env python3
"""
Sawtooth signal demonstration.

Creates a sawtooth signal -> passthrough filter -> plot sink pipeline.
Shows clean data flow without glitches/corruptions.
"""

import time
import numpy as np
import matplotlib.pyplot as plt
import bpipe


class SawtoothSource(bpipe.BpFilterPy):
    """Simple sawtooth generator for demo."""
    
    def __init__(self, frequency=0.01, amplitude=1.0):
        super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)
        self.frequency = frequency
        self.amplitude = amplitude
        self.sample_idx = 0

    def transform(self, inputs, outputs):
        # Generate sawtooth data
        n_samples = 32
        t = np.arange(self.sample_idx, self.sample_idx + n_samples)
        phase = (self.frequency * t) % 1.0
        data = self.amplitude * (2 * phase - 1)
        
        if outputs and len(outputs) > 0:
            copy_len = min(len(data), len(outputs[0]))
            outputs[0][:copy_len] = data[:copy_len].astype(np.float32)
        
        self.sample_idx += n_samples


class Passthrough(bpipe.BpFilterPy):
    """Simple passthrough filter for demonstrating pipeline integrity."""
    
    def __init__(self):
        super().__init__(capacity_exp=10, dtype=bpipe.DTYPE_FLOAT)

    def transform(self, inputs, outputs):
        if inputs and len(inputs[0]) > 0:
            copy_len = min(len(inputs[0]), len(outputs[0]))
            outputs[0][:copy_len] = inputs[0][:copy_len]


def main():
    # Create pipeline components
    signal = SawtoothSource(frequency=0.01, amplitude=1.0)
    passthrough = Passthrough()
    plot_sink = bpipe.PlotSink(max_points=1000)

    # Connect pipeline: signal -> passthrough -> plot_sink
    signal.add_sink(passthrough)
    passthrough.add_sink(plot_sink)

    # Start pipeline
    signal.run()
    passthrough.run()  
    plot_sink.run()

    # Collect data
    print("Collecting sawtooth data...")
    time.sleep(2)

    # Stop pipeline
    signal.stop()
    passthrough.stop()
    plot_sink.stop()

    # Display results
    print("Displaying sawtooth plot...")
    plot_sink.plot(title="Sawtooth Signal Demo")
    plt.show()


if __name__ == "__main__":
    main()
