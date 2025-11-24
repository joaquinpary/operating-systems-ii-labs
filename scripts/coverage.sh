#!/bin/bash

# Enter the build
cd ../build/ || exit 1

# Run the tests
#ctest --output-on-failure
./tests/test_server
#./tests/test_client
./tests/test_common

# Create output folder for coverage
mkdir -p coverage

# Capture coverage
lcov --capture --directory . --output-file coverage/coverage.info\
  --include "*/src/client/*" \
  --include "*/src/common/*" \
  --include "*/src/server/*"

# Filter out irrelevant files like external libs or unity
lcov --remove coverage/coverage.info '*/external/*' '*/tests/*' --output-file coverage/coverage.info

# Generate HTML report
genhtml coverage/coverage.info --output-directory coverage/coverage-report

# Open the report in the browser (optional)
xdg-open coverage/coverage-report/index.html
