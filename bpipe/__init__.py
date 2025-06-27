"""
bpipe - Real-time telemetry data processing framework

This package provides Python bindings for the bpipe C library,
enabling high-performance real-time data processing pipelines.
"""

# Import all from dpcore C extension
from dpcore import (
    BpFilterBase,
    BpFilterPy, 
    BpAggregatorPy,
    BP_WAVE_SQUARE,
    BP_WAVE_SINE,
    BP_WAVE_TRIANGLE,
    BP_WAVE_SAWTOOTH,
    DTYPE_FLOAT,
    DTYPE_INT,
    DTYPE_UNSIGNED
)

# Import simplified filters
from .filters import PlotSink, create_signal_generator

__version__ = "0.0.1"
__all__ = [
    # C extension classes
    "BpFilterBase",
    "BpFilterPy",
    "BpAggregatorPy",
    # Constants
    "BP_WAVE_SQUARE",
    "BP_WAVE_SINE", 
    "BP_WAVE_TRIANGLE",
    "BP_WAVE_SAWTOOTH",
    "DTYPE_FLOAT",
    "DTYPE_INT",
    "DTYPE_UNSIGNED",
    # Python additions
    "PlotSink",
    "create_signal_generator"
]