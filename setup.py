from setuptools import setup, Extension
import os

# Ensure build directory exists
os.makedirs("build", exist_ok=True)

dpcore = Extension(
    name="dpcore",  # Direct module name
    sources=[
        "bpipe/core.c", 
        "bpipe/core_python.c",
        "bpipe/aggregator.c",
        "bpipe/aggregator_python.c",
        "bpipe/signal_gen.c"
    ],
    extra_compile_args=["-g", "-O0"],
    extra_link_args=["-g", "-lm"],  # Add math library for signal generation
)

setup(
    name="bpipe",
    version="0.0.1",
    packages=["bpipe"],
    ext_modules=[dpcore],
    options={
        'build': {
            'build_base': 'build'
        },
        'build_ext': {
            'build_lib': '.'  # Keep the .so file in the root for import compatibility
        }
    }
)