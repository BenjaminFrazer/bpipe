#!/bin/bash
# Setup script to ensure linter versions match CI

set -e

echo "Setting up linters to match CI versions..."

# Check OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    if command -v apt-get &> /dev/null; then
        # Debian/Ubuntu
        echo "Installing clang-format-14 and clang-tidy-14..."
        sudo apt-get update
        sudo apt-get install -y clang-format-14 clang-tidy-14
        
        # Set up alternatives
        sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-14 100
        sudo update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-14 100
    else
        echo "Non-Debian Linux detected. Please install clang-format-14 and clang-tidy-14 manually."
        exit 1
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    echo "macOS detected. Installing LLVM@14 via Homebrew..."
    if ! command -v brew &> /dev/null; then
        echo "Homebrew not found. Please install Homebrew first."
        exit 1
    fi
    
    brew install llvm@14
    echo "Please add the following to your shell profile:"
    echo 'export PATH="/usr/local/opt/llvm@14/bin:$PATH"'
    echo "Or for Apple Silicon Macs:"
    echo 'export PATH="/opt/homebrew/opt/llvm@14/bin:$PATH"'
else
    echo "Unsupported OS: $OSTYPE"
    exit 1
fi

echo ""
echo "Verifying installed versions:"
clang-format --version
clang-tidy --version

echo ""
echo "Setup complete! Your linter versions should now match CI."