"""
Simplified test suite for PlotSink functionality.

Tests core PlotSink behavior with focus on integration and error handling.
"""

import pytest
import numpy as np
from unittest.mock import Mock, patch, MagicMock
import sys
import os

# Add bpipe to path for imports
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'bpipe'))

try:
    import dpcore
    dpcore_available = True
except ImportError:
    dpcore_available = False

from bpipe.filters import PlotSink


@pytest.mark.skipif(not dpcore_available, reason="dpcore module not available")
class TestPlotSink:
    """Test PlotSink functionality with dpcore available."""
    
    def test_plot_sink_creation(self):
        """Test PlotSink can be created with default parameters."""
        sink = PlotSink()
        assert sink is not None
        assert sink.max_points == 10000
        assert not sink.running
        assert sink._aggregator is not None
    
    def test_plot_sink_custom_params(self):
        """Test PlotSink creation with custom parameters."""
        sink = PlotSink(max_capacity_bytes=512*1024*1024, max_points=5000)
        assert sink.max_points == 5000
        assert not sink.running
    
    def test_arrays_property_initial(self):
        """Test arrays property returns list with empty array initially."""
        sink = PlotSink()
        arrays = sink.arrays
        assert isinstance(arrays, list)
        assert len(arrays) >= 0  # May have one empty buffer by default
        if len(arrays) > 0:
            assert all(len(arr) == 0 for arr in arrays)  # All arrays should be empty
    
    def test_sizes_property_initial(self):
        """Test sizes property returns list initially."""
        sink = PlotSink()
        sizes = sink.sizes
        assert isinstance(sizes, list)
        assert len(sizes) >= 0  # May have one buffer by default
        if len(sizes) > 0:
            assert all(size == 0 for size in sizes)  # All buffers should be empty
    
    def test_start_stop_methods_exist(self):
        """Test start and stop methods exist and don't crash."""
        sink = PlotSink()
        
        # These should not raise exceptions
        sink.start()
        assert sink.running
        
        sink.stop()
        assert not sink.running
    
    def test_add_source_method_exists(self):
        """Test add_source method exists."""
        sink = PlotSink()
        
        # Mock source filter
        source_filter = Mock()
        base_filter = Mock()
        source_filter._get_base_filter.return_value = base_filter
        
        # Should not raise an exception
        sink.add_source(source_filter)
    
    def test_clear_method_exists(self):
        """Test clear method exists and doesn't crash."""
        sink = PlotSink()
        # Should not raise an exception
        sink.clear()
    
    def test_plot_no_data_error(self):
        """Test error handling when no data is available."""
        sink = PlotSink()
        
        # Mock matplotlib.pyplot to avoid import issues in testing
        with patch('bpipe.filters.plt', create=True) as mock_plt:
            mock_plt.subplots = Mock()
            mock_plt.cm = Mock()
            
            with pytest.raises(RuntimeError, match="No data available for plotting"):
                sink.plot()
    
    def test_plot_matplotlib_import_error(self):
        """Test error handling when matplotlib is not available."""
        sink = PlotSink()
        
        # Simulate matplotlib import failure by patching the import in the plot method
        import builtins
        original_import = builtins.__import__
        
        def mock_import(name, *args, **kwargs):
            if name == 'matplotlib.pyplot':
                raise ImportError("No module named 'matplotlib'")
            return original_import(name, *args, **kwargs)
        
        with patch('builtins.__import__', side_effect=mock_import):
            with pytest.raises(ImportError, match="matplotlib is required for plotting"):
                sink.plot()
    
    def test_plot_api_structure(self):
        """Test that plot method has the expected API structure."""
        sink = PlotSink()
        
        # Test that plot method exists and accepts expected parameters
        import inspect
        plot_sig = inspect.signature(sink.plot)
        expected_params = ['fig', 'title', 'xlabel', 'ylabel']
        
        for param in expected_params:
            assert param in plot_sig.parameters, f"Missing parameter: {param}"


class TestPlotSinkNoDpcore:
    """Test PlotSink behavior when dpcore is not available."""
    
    @patch('bpipe.filters.dpcore', None)
    def test_plot_sink_no_dpcore(self):
        """Test PlotSink creation when dpcore is not available."""
        sink = PlotSink()
        assert sink._aggregator is None
        assert not sink.running
        assert sink.arrays == []
        assert sink.sizes == []
    
    @patch('bpipe.filters.dpcore', None)
    @patch('bpipe.filters.plt', create=True)
    def test_plot_no_dpcore_error(self, mock_plt):
        """Test plotting error when dpcore is not available."""
        sink = PlotSink()
        
        with pytest.raises(RuntimeError, match="dpcore module not available"):
            sink.plot()


def test_plot_sink_in_filters_module():
    """Test that PlotSink is properly accessible from filters module."""
    from bpipe.filters import PlotSink
    assert PlotSink is not None
    
    # Test that it can be instantiated
    sink = PlotSink()
    assert sink is not None


if __name__ == "__main__":
    pytest.main([__file__])