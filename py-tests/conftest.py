"""
Pytest configuration and shared fixtures for bpipe tests.
"""

import pytest
import dpcore
import numpy as np


@pytest.fixture
def basic_filter():
    """Create a basic filter for testing."""
    filter_obj = dpcore.BpFilterBase(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
    yield filter_obj
    # Cleanup
    if filter_obj.running:
        filter_obj.stop()


@pytest.fixture
def signal_generator():
    """Create a signal generator for testing."""
    from bpipe import create_signal_generator
    gen = create_signal_generator('sine', frequency=1.0, amplitude=1.0)
    yield gen
    # Cleanup
    if gen.running:
        gen.stop()


@pytest.fixture
def aggregator_sink():
    """Create an aggregator sink for testing."""
    agg = dpcore.BpAggregatorPy(n_inputs=1, dtype=dpcore.DTYPE_FLOAT)
    yield agg
    # Cleanup
    if agg.running:
        agg.stop()


@pytest.fixture
def plot_sink():
    """Create a plot sink for testing."""
    from bpipe import PlotSink
    sink = PlotSink()
    yield sink
    # Cleanup
    if sink.running:
        sink.stop()


@pytest.fixture
def sample_data():
    """Generate sample data arrays for testing."""
    return {
        'sine': np.sin(2 * np.pi * np.linspace(0, 1, 100)).astype(np.float32),
        'random': np.random.randn(100).astype(np.float32),
        'ones': np.ones(100, dtype=np.float32),
        'zeros': np.zeros(100, dtype=np.float32),
        'ramp': np.linspace(0, 1, 100).astype(np.float32)
    }


@pytest.fixture
def custom_passthrough_filter():
    """Create a custom passthrough filter."""
    class PassthroughFilter(dpcore.BpFilterPy):
        def __init__(self):
            super().__init__(capacity_exp=10, dtype=dpcore.DTYPE_FLOAT)
            self.samples_processed = 0
        
        def transform(self, inputs, outputs):
            if inputs and outputs and len(inputs) > 0 and len(outputs) > 0:
                input_data = inputs[0]
                outputs[0][:len(input_data)] = input_data
                self.samples_processed += len(input_data)
    
    filter_obj = PassthroughFilter()
    yield filter_obj
    # Cleanup
    if filter_obj.running:
        filter_obj.stop()


@pytest.fixture(autouse=True)
def cleanup_filters(request):
    """Automatically cleanup any running filters after each test."""
    yield
    # This runs after each test
    # Could add global cleanup logic here if needed


# Pytest configuration
def pytest_configure(config):
    """Configure pytest with custom markers."""
    config.addinivalue_line(
        "markers", "slow: marks tests as slow (deselect with '-m \"not slow\"')"
    )
    config.addinivalue_line(
        "markers", "requires_matplotlib: marks tests that require matplotlib"
    )


# Skip conditions
requires_matplotlib = pytest.mark.skipif(
    not pytest.importorskip("matplotlib", reason="matplotlib not installed"),
    reason="requires matplotlib"
)