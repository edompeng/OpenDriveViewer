# Contributing Guide

Thanks for your interest in contributing to OpenDriveViewer.

## Development Principles

1. Follow SOLID principles for new features and module refactors.
2. Keep coupling low between modules and keep each module single-purpose.
3. Prefer C++14/17/20 standard facilities. Avoid adding new third-party libraries without prior discussion.
4. Keep code cross-platform (Windows/Linux/macOS) and avoid deprecated APIs.
5. Optimize for performance and memory efficiency where possible.

## Code Style

1. Follow Google C++ style.
2. If `.clang-format` exists, format code using that configuration.

## Build

Please refer to the [🚀 Building & Testing](./README.md#build-instructions) section in the README for detailed build and test instructions.

## Tests

1. Add or update GoogleTest test cases for all testable modules.
2. Keep tests in `tests/` with `*_test.cpp` naming.
3. Run tests before opening a PR.

## Pull Request Checklist

1. Code compiles successfully.
2. Related tests pass.
3. New behavior includes tests where applicable.
4. Documentation is updated when behavior or structure changes.
