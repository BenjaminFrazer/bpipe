#!/usr/bin/env python3
"""
Test basic dpcore functionality step by step.
"""

import pytest
import dpcore


def test_creation_only():
    """Test just creating filters."""
    filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
    assert filter1 is not None
    
    filter2 = dpcore.BpFilterPy(capacity_exp=10, dtype=2)
    assert filter2 is not None


def test_connection_only():
    """Test connecting filters without starting."""
    filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
    filter2 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
    
    # Test connection
    filter1.add_sink(filter2)
    
    # Test disconnection
    filter1.remove_sink(filter2)


def test_start_only():
    """Test starting a single filter without stopping."""
    filter1 = dpcore.BpFilterBase(capacity_exp=10, dtype=2)
    filter1.run()
    # Note: Not stopping to avoid known issues


if __name__ == "__main__":
    pytest.main([__file__, "-v"])