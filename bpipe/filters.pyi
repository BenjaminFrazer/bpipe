"""
Type stubs for bpipe.filters module.

Provides type hints for factory and wrapper classes.
"""

from typing import Callable, List, Union, Dict, Any
import numpy as np
from .core import BpFilterBase, BpFilterPy

class FilterFactory:
    """Factory class for creating built-in filter types."""
    
    @staticmethod
    def signal_generator(
        waveform: str, 
        frequency: float, 
        amplitude: float = 1.0,
        phase: float = 0.0, 
        x_offset: float = 0.0,
        buffer_size: int = 1024, 
        batch_size: int = 64
    ) -> 'BuiltinFilter':
        """Create a signal generator filter."""
        ...
    
    @staticmethod
    def passthrough(
        buffer_size: int = 1024, 
        batch_size: int = 64
    ) -> 'BuiltinFilter':
        """Create a passthrough filter."""
        ...


class BuiltinFilter:
    """Wrapper for built-in C-implemented filters."""
    
    def __init__(self, filter_type: str, config: Dict[str, Any]) -> None:
        """Initialize built-in filter."""
        ...
    
    def start(self) -> None:
        """Start the filter's worker thread."""
        ...
    
    def stop(self) -> None:
        """Stop the filter's worker thread."""
        ...
    
    def add_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """Connect this filter's output to another filter."""
        ...
    
    def remove_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """Disconnect this filter's output from another filter."""
        ...
    
    def _get_base_filter(self) -> BpFilterBase:
        """Get underlying C filter object."""
        ...
    
    @property
    def running(self) -> bool:
        """Check if filter is currently running."""
        ...
    
    @property 
    def filter_type(self) -> str:
        """Get the filter type."""
        ...
    
    @property
    def config(self) -> Dict[str, Any]:
        """Get the filter configuration."""
        ...


class CustomFilter:
    """Wrapper for user-defined Python transform functions."""
    
    def __init__(
        self, 
        transform_func: Callable[[List[np.ndarray]], List[np.ndarray]], 
        buffer_size: int = 1024, 
        batch_size: int = 64
    ) -> None:
        """Initialize custom filter with user transform function."""
        ...
    
    def start(self) -> None:
        """Start the filter's worker thread."""
        ...
    
    def stop(self) -> None:
        """Stop the filter's worker thread."""
        ...
    
    def add_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """Connect this filter's output to another filter."""
        ...
    
    def remove_sink(self, sink_filter: Union['BuiltinFilter', 'CustomFilter']) -> None:
        """Disconnect this filter's output from another filter."""
        ...
    
    def _get_base_filter(self) -> BpFilterPy:
        """Get underlying C filter object."""
        ...
    
    @property
    def running(self) -> bool:
        """Check if filter is currently running."""
        ...
    
    @property
    def transform_func(self) -> Callable[[List[np.ndarray]], List[np.ndarray]]:
        """Get the transform function."""
        ...