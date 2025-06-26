"""
bpipe - Real-time telemetry data processing framework

This package provides Python bindings for the bpipe C library,
enabling high-performance real-time data processing pipelines.
"""

# Note: dpcore is the C extension module, not a submodule
from .filters import FilterFactory, CustomFilter

__version__ = "0.0.1"
__all__ = ["FilterFactory", "CustomFilter"]