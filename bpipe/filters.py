"""
Simplified bpipe filters module.

Provides PlotSink and helper functions for creating filters.
"""

import dpcore
import numpy as np


def create_signal_generator(waveform, frequency, amplitude=1.0, phase=0.0, x_offset=0.0,
                          buffer_size=1024, batch_size=64):
    """
    Create a signal generator filter.
    
    Note: This is a placeholder until SignalGenerator is exposed from C.
    For now, users should create custom Python filters for signal generation.
    """
    # Map string waveforms to constants
    waveform_map = {
        'square': dpcore.BP_WAVE_SQUARE,
        'sine': dpcore.BP_WAVE_SINE,
        'triangle': dpcore.BP_WAVE_TRIANGLE,
        'sawtooth': dpcore.BP_WAVE_SAWTOOTH
    }

    if isinstance(waveform, str):
        if waveform not in waveform_map:
            raise ValueError(f"Unknown waveform: {waveform}")
        waveform = waveform_map[waveform]

    # For now, create a custom filter that generates the signal
    class SignalGen(dpcore.BpFilterPy):
        def __init__(self):
            super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
            self.waveform = waveform
            self.frequency = frequency
            self.amplitude = amplitude
            self.phase = phase
            self.x_offset = x_offset
            self.sample_idx = 0

        def transform(self, inputs, outputs):
            # For a source filter, generate data into the first output
            if outputs and len(outputs) > 0:
                # Generate batch_size samples
                n_samples = min(batch_size, len(outputs[0]) if hasattr(outputs[0], '__len__') else batch_size)
                t = np.arange(self.sample_idx, self.sample_idx + n_samples, dtype=np.float32)

                if self.waveform == dpcore.BP_WAVE_SINE:
                    data = self.amplitude * np.sin(2 * np.pi * self.frequency * t + self.phase) + self.x_offset
                elif self.waveform == dpcore.BP_WAVE_SQUARE:
                    data = self.amplitude * np.sign(np.sin(2 * np.pi * self.frequency * t + self.phase)) + self.x_offset
                elif self.waveform == dpcore.BP_WAVE_TRIANGLE:
                    # Triangle wave using arcsin
                    data = self.amplitude * (2/np.pi) * np.arcsin(np.sin(2 * np.pi * self.frequency * t + self.phase)) + self.x_offset
                elif self.waveform == dpcore.BP_WAVE_SAWTOOTH:
                    # Sawtooth using modulo
                    data = self.amplitude * (2 * ((self.frequency * t + self.phase) % 1) - 1) + self.x_offset

                # Ensure data is float32 to match DTYPE_FLOAT
                data = data.astype(np.float32)
                outputs[0][:n_samples] = data
                self.sample_idx += n_samples

    return SignalGen()


class PlotSink(dpcore.BpAggregatorPy):
    """Matplotlib plotting sink built on top of the aggregator class."""

    def __init__(self, max_capacity_bytes=1024*1024*1024, max_points=10000, **kwargs):
        """
        Initialize plotting sink.
        
        Args:
            max_capacity_bytes: Maximum memory per buffer (default 1GB)
            max_points: Maximum points to plot for performance (default 10k)
            **kwargs: Additional arguments passed to BpAggregatorPy
        """
        super().__init__(max_capacity_bytes=max_capacity_bytes, **kwargs)
        self.max_points = max_points

    def plot(self, fig=None, title="Signal Plot", xlabel="Sample Index",
             ylabel="Amplitude", **plot_kwargs):
        """
        Create a matplotlib plot of all aggregated data.
        
        Args:
            fig: Optional matplotlib Figure object. If None, creates new figure.
            title: Plot title
            xlabel: X-axis label (defaults to "Sample Index")
            ylabel: Y-axis label  
            **plot_kwargs: Additional arguments passed to plt.plot()
            
        Returns:
            matplotlib Figure object
            
        Raises:
            ImportError: If matplotlib is not available
            RuntimeError: If no data has been aggregated
        """
        import matplotlib.pyplot as plt

        # Get aggregated data arrays
        arrays = self.arrays
        if not arrays or all(len(arr) == 0 for arr in arrays):
            raise RuntimeError("No data available for plotting")

        # Create figure if not provided
        if fig is None:
            fig, ax = plt.subplots(figsize=(10, 6))
        else:
            ax = fig.gca() if len(fig.axes) == 0 else fig.axes[0]

        # Plot each input buffer as a separate trace
        colors = plt.cm.tab10(np.linspace(0, 1, len(arrays)))

        for i, data_array in enumerate(arrays):
            if len(data_array) == 0:
                continue

            # Create sample index x-axis
            x_data = np.arange(len(data_array))
            y_data = data_array

            # Downsample if data is too large for performance
            if len(data_array) > self.max_points:
                step = len(data_array) // self.max_points
                x_data = x_data[::step]
                y_data = y_data[::step]

            # Plot the trace with auto-generated color
            ax.plot(x_data, y_data, color=colors[i], label=f'Input {i}', **plot_kwargs)

        # Configure plot appearance
        ax.set_title(title)
        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)
        ax.grid(True, alpha=0.3)

        # Add legend if multiple traces
        if len([arr for arr in arrays if len(arr) > 0]) > 1:
            ax.legend()

        # Adjust layout and return figure
        fig.tight_layout()
        return fig
