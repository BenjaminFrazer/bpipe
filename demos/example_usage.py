#!/usr/bin/env python3
"""
Example usage of bpipe Python API.

This example demonstrates:
1. Creating built-in filters using the factory pattern
2. Creating custom filters with user-defined transform functions  
3. Connecting filters together in a processing pipeline
4. Starting/stopping filter execution
"""

import numpy as np
import time
from bpipe import FilterFactory, CustomFilter


def main():
    print("bpipe Python API Example")
    print("=" * 30)
    
    # Example 1: Signal Generator -> Custom Filter -> Built-in Passthrough
    print("\n1. Creating signal generator...")
    
    # Create a sawtooth wave generator
    signal_gen = FilterFactory.signal_generator(
        waveform='sawtooth',
        frequency=0.01,  # 1 cycle per 100 samples
        amplitude=100.0,
        phase=0.0,
        x_offset=50.0
    )
    
    print(f"   Signal generator created: {signal_gen.filter_type}")
    print(f"   Configuration: {signal_gen.config}")
    
    # Example 2: Custom Filter with User Transform Function
    print("\n2. Creating custom filter...")
    
    def scaling_transform(inputs):
        """Custom transform that scales and adds offset to input."""
        if not inputs or inputs[0] is None:
            return [np.array([])]
        
        # Scale by 2 and add offset of 10
        scaled = inputs[0] * 2.0 + 10.0
        return [scaled]
    
    scaler = CustomFilter(
        transform_func=scaling_transform,
        buffer_size=1024,
        batch_size=64
    )
    
    print(f"   Custom filter created with scaling transform")
    
    # Example 3: Built-in Passthrough Filter
    print("\n3. Creating passthrough filter...")
    
    passthrough = FilterFactory.passthrough(
        buffer_size=1024,
        batch_size=64
    )
    
    print(f"   Passthrough filter created: {passthrough.filter_type}")
    
    # Example 4: Connect filters in a pipeline
    print("\n4. Connecting filters in pipeline...")
    print("   Pipeline: SignalGen -> Scaler -> Passthrough")
    
    # Connect signal generator to custom scaler
    signal_gen.add_sink(scaler)
    
    # Connect scaler to passthrough
    scaler.add_sink(passthrough)
    
    print("   Filters connected successfully")
    
    # Example 5: Start processing pipeline
    print("\n5. Starting processing pipeline...")
    
    try:
        # Start filters in reverse order (sink to source)
        passthrough.start()
        scaler.start()  
        signal_gen.start()
        
        print("   All filters started")
        print(f"   Signal generator running: {signal_gen.running}")
        print(f"   Scaler running: {scaler.running}")
        print(f"   Passthrough running: {passthrough.running}")
        
        # Let it run for a short time
        print("\n   Processing data for 2 seconds...")
        time.sleep(2)
        
    except Exception as e:
        print(f"   Error during processing: {e}")
    
    # Example 6: Stop processing pipeline
    print("\n6. Stopping processing pipeline...")
    
    try:
        # Stop filters in forward order (source to sink)
        signal_gen.stop()
        scaler.stop()
        passthrough.stop()
        
        print("   All filters stopped")
        print(f"   Signal generator running: {signal_gen.running}")
        print(f"   Scaler running: {scaler.running}")
        print(f"   Passthrough running: {passthrough.running}")
        
    except Exception as e:
        print(f"   Error during shutdown: {e}")
    
    # Example 7: Demonstrate filter disconnection
    print("\n7. Demonstrating filter disconnection...")
    
    # Remove connection between signal generator and scaler
    signal_gen.remove_sink(scaler)
    print("   Disconnected signal generator from scaler")
    
    # Reconnect for completeness
    signal_gen.add_sink(scaler)
    print("   Reconnected signal generator to scaler")
    
    print("\n" + "=" * 30)
    print("Example completed successfully!")
    print("\nThis example demonstrates the bpipe Python API:")
    print("- Factory pattern for built-in filters")
    print("- Custom filters with user transform functions")  
    print("- Filter pipeline construction and management")
    print("- Start/stop lifecycle management")


def advanced_example():
    """Advanced example showing more complex transforms."""
    print("\n" + "=" * 40)
    print("Advanced Example: Multi-input Processing")
    print("=" * 40)
    
    # Create multiple signal generators
    sine_gen = FilterFactory.signal_generator('sine', 0.05, 50.0)
    square_gen = FilterFactory.signal_generator('square', 0.03, 30.0)
    
    # Custom filter that combines multiple inputs
    def mixer_transform(inputs):
        """Mix multiple input signals."""
        if len(inputs) < 2 or inputs[0] is None or inputs[1] is None:
            return [np.array([])]
        
        # Mix the two signals with weights
        mixed = 0.6 * inputs[0] + 0.4 * inputs[1]
        return [mixed]
    
    mixer = CustomFilter(mixer_transform)
    
    # Custom filter for frequency analysis  
    def fft_transform(inputs):
        """Compute FFT magnitude of input."""
        if not inputs or inputs[0] is None or len(inputs[0]) == 0:
            return [np.array([])]
        
        # Compute FFT magnitude
        fft_result = np.abs(np.fft.fft(inputs[0]))
        # Return first half (positive frequencies)
        return [fft_result[:len(fft_result)//2]]
    
    fft_analyzer = CustomFilter(fft_transform)
    
    print("Created multi-input processing pipeline:")
    print("SineGen \\")
    print("         -> Mixer -> FFT Analyzer")  
    print("SquareGen /")
    
    # Connect pipeline
    sine_gen.add_sink(mixer)
    square_gen.add_sink(mixer)
    mixer.add_sink(fft_analyzer)
    
    print("Advanced pipeline created successfully!")


if __name__ == "__main__":
    main()
    advanced_example()