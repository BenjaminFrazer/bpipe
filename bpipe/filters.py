"""
High-level Python wrapper for bpipe filters.

Provides factory-based filter creation and custom filter support.
"""

try:
    import dpcore
except ImportError:
    # Fallback for testing without proper installation
    dpcore = None
from typing import Callable, List, Optional, Union, Any
import numpy as np


class FilterFactory:
    """Factory class for creating built-in filter types."""
    
    @staticmethod
    def signal_generator(waveform: str, frequency: float, amplitude: float = 1.0, 
                        phase: float = 0.0, x_offset: float = 0.0,
                        buffer_size: int = 1024, batch_size: int = 64) -> 'BuiltinFilter':
        """
        Create a signal generator filter.
        
        Args:
            waveform: Type of waveform ('square', 'sine', 'triangle', 'sawtooth')
            frequency: Signal frequency in cycles per sample
            amplitude: Signal amplitude
            phase: Initial phase offset in cycles  
            x_offset: DC offset
            buffer_size: Output buffer size
            batch_size: Batch processing size
            
        Returns:
            BuiltinFilter configured as signal generator
        """
        waveform_map = {
            'square': 0,    # BP_WAVE_SQUARE
            'sine': 1,      # BP_WAVE_SINE  
            'triangle': 2,  # BP_WAVE_TRIANGLE
            'sawtooth': 3   # BP_WAVE_SAWTOOTH
        }
        
        if waveform not in waveform_map:
            raise ValueError(f"Unknown waveform: {waveform}. Must be one of {list(waveform_map.keys())}")
            
        return BuiltinFilter(
            filter_type='signal_generator',
            config={
                'waveform': waveform_map[waveform],
                'frequency': frequency,
                'amplitude': amplitude, 
                'phase': phase,
                'x_offset': x_offset,
                'buffer_size': buffer_size,
                'batch_size': batch_size
            }
        )
    
    @staticmethod
    def passthrough(buffer_size: int = 1024, batch_size: int = 64) -> 'BuiltinFilter':
        """
        Create a passthrough filter that copies input to output.
        
        Args:
            buffer_size: Buffer size for processing
            batch_size: Batch processing size
            
        Returns:
            BuiltinFilter configured as passthrough
        """
        return BuiltinFilter(
            filter_type='passthrough',
            config={
                'buffer_size': buffer_size,
                'batch_size': batch_size
            }
        )


class BuiltinFilter:
    """Wrapper for built-in C-implemented filters."""
    
    def __init__(self, filter_type: str, config: dict):
        """
        Initialize built-in filter.
        
        Args:
            filter_type: Type of built-in filter
            config: Configuration parameters
        """
        self._filter_type = filter_type
        self._config = config
        self._base_filter = None
        self._running = False
        
        # Create the underlying C filter based on type
        if filter_type == 'signal_generator':
            self._base_filter = self._create_signal_generator()
        elif filter_type == 'passthrough':
            self._base_filter = self._create_passthrough()
        else:
            raise ValueError(f"Unknown filter type: {filter_type}")
    
    def _create_signal_generator(self):
        """Create signal generator filter instance."""
        # For now, create a base filter - will need C extension support
        # This is a placeholder that shows the intended interface
        if dpcore is None:
            return None
        filter_obj = dpcore.BpFilterBase(capacity_exp=10, dtype=2)  # DTYPE_UNSIGNED
        return filter_obj
    
    def _create_passthrough(self):
        """Create passthrough filter instance.""" 
        if dpcore is None:
            return None
        filter_obj = dpcore.BpFilterBase(capacity_exp=10, dtype=2)  # DTYPE_UNSIGNED
        return filter_obj
    
    def start(self) -> None:
        """Start the filter's worker thread."""
        if self._base_filter and not self._running:
            self._base_filter.run()
            self._running = True
    
    def stop(self) -> None:
        """Stop the filter's worker thread."""
        if self._base_filter and self._running:
            self._base_filter.stop()
            self._running = False
    
    def add_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """
        Connect this filter's output to another filter's input.
        
        Args:
            sink_filter: Filter to receive this filter's output
        """
        if self._base_filter:
            sink_base = sink_filter._get_base_filter()
            self._base_filter.add_sink(sink_base)
    
    def remove_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """
        Disconnect this filter's output from another filter.
        
        Args:
            sink_filter: Filter to disconnect
        """
        if self._base_filter:
            sink_base = sink_filter._get_base_filter()
            self._base_filter.remove_sink(sink_base)
    
    def _get_base_filter(self) -> dpcore.BpFilterBase:
        """Get underlying C filter object."""
        return self._base_filter
    
    @property
    def running(self) -> bool:
        """Check if filter is currently running."""
        return self._running
    
    @property 
    def filter_type(self) -> str:
        """Get the filter type."""
        return self._filter_type
    
    @property
    def config(self) -> dict:
        """Get the filter configuration."""
        return self._config.copy()


class CustomFilter:
    """Wrapper for user-defined Python transform functions."""
    
    def __init__(self, transform_func: Callable[[List[np.ndarray]], List[np.ndarray]], 
                 buffer_size: int = 1024, batch_size: int = 64):
        """
        Initialize custom filter with user transform function.
        
        Args:
            transform_func: Python function that transforms input arrays to output arrays
            buffer_size: Buffer size for processing  
            batch_size: Batch processing size
        """
        self._transform_func = transform_func
        self._buffer_size = buffer_size
        self._batch_size = batch_size
        self._base_filter = None
        self._running = False
        
        # Create Python filter with custom transform
        if dpcore is None:
            self._base_filter = None
        else:
            # Create a dynamic class that inherits from BpFilterPy
            class UserFilter(dpcore.BpFilterPy):
                def __init__(self, user_func, *args, **kwargs):
                    super().__init__(*args, **kwargs)
                    self.user_func = user_func
                
                def transform(self, outputs, inputs, timestamp):
                    # Call the user transform function
                    try:
                        results = self.user_func(inputs)
                        
                        # Copy results to output arrays
                        if isinstance(results, list):
                            for i, result in enumerate(results):
                                if i < len(outputs) and result is not None and len(result) > 0:
                                    copy_len = min(len(result), len(outputs[i]))
                                    outputs[i][:copy_len] = result[:copy_len]
                        else:
                            # Single output case
                            if len(outputs) > 0 and results is not None and len(results) > 0:
                                copy_len = min(len(results), len(outputs[0]))
                                outputs[0][:copy_len] = results[:copy_len]
                    except Exception as e:
                        print(f"Error in custom transform: {e}")
            
            self._base_filter = UserFilter(transform_func, capacity_exp=10, dtype=2)
    
    
    def start(self) -> None:
        """Start the filter's worker thread."""
        if self._base_filter and not self._running:
            self._base_filter.run()
            self._running = True
    
    def stop(self) -> None:
        """Stop the filter's worker thread."""
        if self._base_filter and self._running:
            self._base_filter.stop()
            self._running = False
    
    def add_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """
        Connect this filter's output to another filter's input.
        
        Args:
            sink_filter: Filter to receive this filter's output
        """
        if self._base_filter:
            sink_base = sink_filter._get_base_filter()
            self._base_filter.add_sink(sink_base)
    
    def remove_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """
        Disconnect this filter's output from another filter.
        
        Args:
            sink_filter: Filter to disconnect  
        """
        if self._base_filter:
            sink_base = sink_filter._get_base_filter()
            self._base_filter.remove_sink(sink_base)
    
    def _get_base_filter(self) -> dpcore.BpFilterPy:
        """Get underlying C filter object."""
        return self._base_filter
    
    @property
    def running(self) -> bool:
        """Check if filter is currently running."""
        return self._running
    
    @property
    def transform_func(self) -> Callable:
        """Get the transform function."""
        return self._transform_func