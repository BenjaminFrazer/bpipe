#!/usr/bin/env python3
"""
Demo script showing signal generator → PlotSink pipeline.

Demonstrates the complete data flow using a mock signal generator
that will be replaced with the real C implementation.
"""

import sys
import os
import numpy as np
import time

# Add bpipe to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    print("✓ dpcore module loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import dpcore: {e}")
    print("Run 'python setup.py build_ext --inplace' first")
    sys.exit(1)

try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    print("✓ matplotlib loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import matplotlib: {e}")
    print("Run 'pip install matplotlib' first")  
    sys.exit(1)

from bpipe.filters import PlotSink, CustomFilter


class MockSignalGenerator:
    """
    Mock signal generator that simulates the real C implementation.
    
    This generates the same waveforms that the C signal generator would produce,
    demonstrating the complete pipeline before the C integration is finished.
    """
    
    def __init__(self, waveform, frequency, amplitude=1.0, phase=0.0, x_offset=0.0):
        self.waveform = waveform
        self.frequency = frequency
        self.amplitude = amplitude
        self.phase = phase
        self.x_offset = x_offset
        self.sample_idx = 0
        
        # Map waveform names to constants
        self.waveform_map = {
            'square': dpcore.BP_WAVE_SQUARE,
            'sine': dpcore.BP_WAVE_SINE,
            'triangle': dpcore.BP_WAVE_TRIANGLE,
            'sawtooth': dpcore.BP_WAVE_SAWTOOTH
        }
        
        if isinstance(waveform, str):
            self.waveform_id = self.waveform_map.get(waveform, dpcore.BP_WAVE_SINE)
        else:
            self.waveform_id = waveform
    
    def generate_batch(self, batch_size):
        """Generate a batch of samples."""
        samples = np.zeros(batch_size, dtype=np.float32)
        
        for i in range(batch_size):
            # Calculate phase for this sample
            current_phase = (self.sample_idx * self.frequency + self.phase) % 1.0
            
            # Generate waveform based on type (matching C implementation)
            if self.waveform_id == dpcore.BP_WAVE_SQUARE:
                value = self.amplitude if current_phase < 0.5 else -self.amplitude
            elif self.waveform_id == dpcore.BP_WAVE_SINE:
                value = self.amplitude * np.sin(2 * np.pi * current_phase)
            elif self.waveform_id == dpcore.BP_WAVE_TRIANGLE:
                # Triangle: 4 * |frac - 0.5| - 1, scaled by amplitude
                value = self.amplitude * (4 * abs(current_phase - 0.5) - 1)
            elif self.waveform_id == dpcore.BP_WAVE_SAWTOOTH:
                # Sawtooth: direct mapping of fractional phase
                value = self.amplitude * (2 * current_phase - 1)
            else:
                value = 0.0
            
            samples[i] = value + self.x_offset
            self.sample_idx += 1
        
        return samples


def create_signal_generator_filter(waveform, frequency, amplitude=1.0, phase=0.0, x_offset=0.0):
    """
    Create a signal generator filter using CustomFilter.
    
    This will be replaced with the real C signal generator when integration is complete.
    """
    # Create the mock signal generator
    sig_gen = MockSignalGenerator(waveform, frequency, amplitude, phase, x_offset)
    
    def signal_transform(inputs):
        """Transform function that generates signal data."""
        # Signal generators don't use inputs - they generate data
        batch_size = 64  # Default batch size
        signal_data = sig_gen.generate_batch(batch_size)
        return [signal_data]  # Return as list for multi-output support
    
    # Create CustomFilter with signal generation transform
    return CustomFilter(signal_transform, buffer_size=1024, batch_size=64)


def demo_signal_to_plot():
    """Demonstrate signal generator → PlotSink pipeline."""
    print("\n=== Signal Generator → PlotSink Demo ===")
    
    # Create mock signal generators with different waveforms
    print("Creating signal generators...")
    sine_gen = create_signal_generator_filter('sine', frequency=0.02, amplitude=1.0)
    square_gen = create_signal_generator_filter('square', frequency=0.015, amplitude=0.8)
    
    # Create PlotSink to collect and plot data
    print("Creating PlotSink...")
    plot_sink = PlotSink(max_capacity_bytes=64*1024, max_points=1000)
    
    # Connect signal generators to plot sink
    print("Connecting filters...")
    plot_sink.add_source(sine_gen)
    plot_sink.add_source(square_gen)
    
    # Start the pipeline
    print("Starting pipeline...")
    sine_gen.start()
    square_gen.start()
    plot_sink.start()
    
    # Let it run for a bit to generate data
    print("Generating data...")
    time.sleep(2.0)
    
    # Stop the pipeline
    print("Stopping pipeline...")
    sine_gen.stop()
    square_gen.stop()
    plot_sink.stop()
    
    # Check how much data we collected
    arrays = plot_sink.arrays
    sizes = plot_sink.sizes
    print(f"Collected data: {len(arrays)} streams, sizes: {sizes}")
    
    # Create and save plot
    if all(size > 0 for size in sizes):
        print("Creating plot...")
        try:
            fig = plot_sink.plot(
                title="Signal Generator Demo",
                xlabel="Sample Index",
                ylabel="Amplitude"
            )
            
            # Save plot to file
            output_file = "signal_demo_plot.png"
            fig.savefig(output_file, dpi=150, bbox_inches='tight')
            print(f"✓ Plot saved to {output_file}")
            
            # Show some statistics
            for i, arr in enumerate(arrays):
                if len(arr) > 0:
                    print(f"Stream {i}: {len(arr)} samples, range [{arr.min():.3f}, {arr.max():.3f}]")
            
            return True
            
        except Exception as e:
            print(f"✗ Plotting failed: {e}")
            return False
    else:
        print("✗ No data collected - pipeline may not be working correctly")
        return False


def demo_waveform_comparison():
    """Demo showing all four waveform types."""
    print("\n=== Waveform Comparison Demo ===")
    
    # Create all four waveform types
    waveforms = [
        ('sine', dpcore.BP_WAVE_SINE),
        ('square', dpcore.BP_WAVE_SQUARE), 
        ('triangle', dpcore.BP_WAVE_TRIANGLE),
        ('sawtooth', dpcore.BP_WAVE_SAWTOOTH)
    ]
    
    # Generate short samples of each waveform for comparison
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    axes = axes.flatten()
    
    for idx, (waveform_name, waveform_id) in enumerate(waveforms):
        sig_gen = MockSignalGenerator(waveform_id, frequency=0.1, amplitude=1.0)
        samples = sig_gen.generate_batch(100)
        
        ax = axes[idx]
        ax.plot(range(len(samples)), samples, 'b-', linewidth=2)
        ax.set_title(f'{waveform_name.title()} Wave (ID: {waveform_id})')
        ax.set_xlabel('Sample Index')
        ax.set_ylabel('Amplitude')
        ax.grid(True, alpha=0.3)
        ax.set_ylim(-1.2, 1.2)
    
    plt.tight_layout()
    plt.savefig('waveform_comparison.png', dpi=150, bbox_inches='tight')
    print("✓ Waveform comparison saved to waveform_comparison.png")
    
    return True


def main():
    """Main demo function."""
    print("Signal Generator → PlotSink Pipeline Demo")
    print("=" * 50)
    print(f"Using waveform constants:")
    print(f"  SINE: {dpcore.BP_WAVE_SINE}")
    print(f"  SQUARE: {dpcore.BP_WAVE_SQUARE}")
    print(f"  TRIANGLE: {dpcore.BP_WAVE_TRIANGLE}")
    print(f"  SAWTOOTH: {dpcore.BP_WAVE_SAWTOOTH}")
    
    try:
        # Run both demos
        success1 = demo_signal_to_plot()
        success2 = demo_waveform_comparison()
        
        if success1 and success2:
            print(f"\n🎉 All demos completed successfully!")
            print(f"📊 Generated plots demonstrate the complete pipeline:")
            print(f"   - signal_demo_plot.png: Real-time pipeline demo")
            print(f"   - waveform_comparison.png: All waveform types")
            print(f"\n🔧 Next step: Replace MockSignalGenerator with real C implementation")
        else:
            print(f"\n⚠️  Some demos had issues, but basic functionality works")
        
        return 0
        
    except Exception as e:
        print(f"\n💥 Demo failed: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())