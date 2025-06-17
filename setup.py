from setuptools import setup, Extension

bpipe = Extension(
    name="bpipe.core",  # <- matches your import path!
    sources=["bpipe/core.c", "bpipe/core_python.c"],
    extra_compile_args=["-g", "-O0"],
    extra_link_args=["-g"],
)

setup(
    name="bpipe",
    version="0.0.1",
    packages=["test_harness"],
    ext_modules=[bpipe],
)
