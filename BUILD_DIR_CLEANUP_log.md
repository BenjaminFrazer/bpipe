# BUILD_DIR_CLEANUP Task Log

## Summary
Modified the build system to output all build artifacts to a `build/` directory instead of cluttering the root directory.

## Changes Made

### 1. Makefile Modifications
- Added `BUILD_DIR=build` variable
- Modified compiler flags to use `-save-temps=obj` to place intermediate files in build directory
- Updated all build rules to create objects and executables in `$(BUILD_DIR)/`
- Added directory creation rule `$(BUILD_DIR):` with order-only prerequisite
- Updated pattern rules for building object files from source
- Modified `clean` target to remove entire build directory
- Updated `run` target to execute tests from build directory

### 2. setup.py Modifications
- Added build directory configuration options
- Set `build_base` to 'build' for all Python build artifacts
- Kept `.so` file in root directory for import compatibility
- Added `os.makedirs("build", exist_ok=True)` to ensure directory exists

### 3. .gitignore Cleanup
- Removed redundant entries for individual test executables
- Kept `build/` entry to exclude all build artifacts
- Cleaned up duplicate entries since artifacts are now centralized

## Testing
- Successfully built all C tests with `make all`
- All build artifacts (.o, .i, .s files and executables) are now in `build/` directory
- Python extension builds correctly with intermediate files in `build/`
- Tests run successfully from new location (though one test failure was noted, unrelated to build changes)

## Result
The root directory is now clean of build artifacts, making the project structure more organized and easier to navigate.