#!/usr/bin/env python3
"""
Demo script showing signal generator → PlotSink pipeline.

Demonstrates complete data flow with multiple waveforms.
"""

import time
import matplotlib.pyplot as plt
import bpipe

def demo_signal_to_plot():
    """Demonstrate signal generator → PlotSink pipeline."""
    print("Creating signal generators...")
    sine_gen = bpipe.create_signal_generator('sine', frequency=0.02, amplitude=1.0)
    square_gen = bpipe.create_signal_generator('square', frequency=0.015, amplitude=0.8)

    print("Creating PlotSink...")
    plot_sink = bpipe.PlotSink(max_points=1000)

    print("Connecting filters...")
    sine_gen.add_sink(plot_sink)
    square_gen.add_sink(plot_sink)

    print("Starting pipeline...")
    sine_gen.run()
    square_gen.run()
    plot_sink.run()

    print("Generating data...")
    time.sleep(2.0)

    print("Stopping pipeline...")
    sine_gen.stop()
    square_gen.stop()
    plot_sink.stop()

    print("Creating plot...")
    fig = plot_sink.plot(title="Signal Generator Demo")
    fig.savefig('signal_demo_plot.png', dpi=150, bbox_inches='tight')
    print("Plot saved to signal_demo_plot.png")

    return fig


def demo_waveform_comparison():
    """Demo showing all four waveform types."""
    print("\nWaveform comparison demo...")
    
    waveforms = ['sine', 'square', 'triangle', 'sawtooth']
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    axes = axes.flatten()

    for idx, waveform in enumerate(waveforms):
        gen = bpipe.create_signal_generator(waveform, frequency=0.1, amplitude=1.0)
        sink = bpipe.PlotSink(max_points=100)
        
        gen.add_sink(sink)
        gen.run()
        sink.run()
        
        time.sleep(0.5)
        
        gen.stop()
        sink.stop()
        
        # Plot this waveform
        ax = axes[idx]
        if sink.arrays and len(sink.arrays[0]) > 0:
            data = sink.arrays[0]
            ax.plot(range(len(data)), data, 'b-', linewidth=2)
        
        ax.set_title(f'{waveform.title()} Wave')
        ax.set_xlabel('Sample Index')
        ax.set_ylabel('Amplitude')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(-1.2, 1.2)

    plt.tight_layout()
    plt.savefig('waveform_comparison.png', dpi=150, bbox_inches='tight')
    print("Waveform comparison saved to waveform_comparison.png")
    return fig


def main():
    """Main demo function."""
    print("Signal Generator → PlotSink Pipeline Demo")
    print("=" * 50)

    demo_signal_to_plot()
    demo_waveform_comparison()
    
    print("\nDemo completed successfully!")
    print("Generated plots:")
    print("   - signal_demo_plot.png: Multi-signal pipeline demo")
    print("   - waveform_comparison.png: All waveform types")


if __name__ == "__main__":
    main()
