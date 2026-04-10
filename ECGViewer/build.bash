#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$script_dir/build"

clean_build() {
    echo "Cleaning build directory..."

    if [[ -d "$build_dir" ]]; then
        (
            cd "$build_dir"

            if [[ -f Makefile ]]; then
                echo "Running 'make clean'..."
                make clean
            else
                echo "No Makefile found in build directory, skipping 'make clean'."
            fi
        )

        echo "Removing build directory..."
        rm -rf "$build_dir"
    else
        echo "No build directory to clean."
    fi

    echo "Cleaned up build."
}

configure_and_build() {
    echo "Creating build directory..."
    mkdir -p "$build_dir"

    (
        cd "$build_dir"
        echo "Running CMake..."
        cmake ..

        echo "Building..."
        make -j"$(nproc)"
    )
}


help() {
  echo "Useage build.bash [--clean | --clean-build]"
  echo ""
  echo "--clean: Clean up all build files and pybind files"
  echo "--clean-build: Clean up all build files and then do a fresh build"
}

ACTION="build"   # default action

if [[ $# -gt 0 ]]; then
    case "$1" in
        --clean)
            ACTION="clean"
            ;;
        --clean-build)
            ACTION="clean_and_build"
            ;;
        --help)
            ACTION="help"
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--clean]"
            exit 1
            ;;
    esac
fi

case "$ACTION" in
    clean)
        clean_build
        ;;
    build)
        configure_and_build
        ;;
    clean_and_build)
        clean_build
        configure_and_build
        ;;
    help)
        help
        ;;
esac

