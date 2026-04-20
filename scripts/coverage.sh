#!/bin/bash

cd "$(dirname "$0")/../build/" || exit 1

# Export DB env vars so unit tests can connect to the local Docker DB
export POSTGRES_HOST="${POSTGRES_HOST:-localhost}"
export POSTGRES_PORT="${POSTGRES_PORT:-5434}"
export POSTGRES_DB="${POSTGRES_DB:-dhl_db}"
export POSTGRES_USER="${POSTGRES_USER:-dhl_user}"
export POSTGRES_PASSWORD="${POSTGRES_PASSWORD:-dhl_pass}"

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
