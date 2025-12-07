#!/bin/bash

cd "$(dirname "$0")/../build/" || exit 1

echo "Running tests..."
for test_exec in tests/test_*; do
    if [ -f "$test_exec" ] && [ -x "$test_exec" ]; then
        echo "Executing $test_exec"
        "$test_exec"
    fi
done

mkdir -p coverage

lcov --capture --directory . --output-file coverage/coverage.info \
  --include "*/src/client/*" \
  --include "*/src/common/*" \
  --include "*/src/server/*"

lcov --remove coverage/coverage.info '*/external/*' '*/tests/*' --output-file coverage/coverage.info

genhtml coverage/coverage.info --output-directory coverage/coverage-report

if command -v xdg-open &> /dev/null; then
    xdg-open coverage/coverage-report/index.html
fi
