"""
Type stubs for bpipe.core module (dpcore C extension).

Provides type hints for auto-completion and static analysis.
"""

from typing import Optional, Any
import numpy as np

class BpFilterBase:
    """Base filter class wrapping C Bp_Filter_t."""
    
    def __init__(self, capacity_exp: int, dtype: int = 2) -> None:
        """
        Initialize base filter.
        
        Args:
            capacity_exp: Ring buffer capacity exponent (buffer size = 2^capacity_exp)
            dtype: Data type (0=float, 1=int, 2=unsigned)
        """
        ...
    
    def run(self) -> None:
        """Start the filter's worker thread."""
        ...
    
    def stop(self) -> None:
        """Stop the filter's worker thread."""
        ...
    
    def add_sink(self, sink: 'BpFilterBase') -> None:
        """Add a sink filter to receive this filter's output."""
        ...
    
    def remove_sink(self, sink: 'BpFilterBase') -> None:
        """Remove a sink filter."""
        ...
    
    def add_source(self, source: 'BpFilterBase') -> None:
        """Add a source filter to provide input to this filter."""
        ...
    
    def remove_source(self, source: 'BpFilterBase') -> None:
        """Remove a source filter."""
        ...
    
    def set_sink(self, sink: 'BpFilterBase') -> None:
        """Set sink filter (backward compatibility)."""
        ...


class BpFilterPy(BpFilterBase):
    """Python transform filter allowing custom Python transform functions."""
    
    def __init__(self, capacity_exp: int, dtype: int = 2) -> None:
        """
        Initialize Python filter.
        
        Args:
            capacity_exp: Ring buffer capacity exponent
            dtype: Data type (0=float, 1=int, 2=unsigned)
        """
        ...
    
    def transform(self, outputs: list[np.ndarray], inputs: list[np.ndarray], timestamp: int) -> None:
        """
        Transform function to be overridden by user.
        
        Args:
            outputs: List of output numpy arrays to fill
            inputs: List of input numpy arrays to process
            timestamp: Timestamp for the batch
        """
        ...