#!/bin/bash

# Script to format C/C++ code with clang-format
set -eo pipefail

# Configuration
SOURCE_DIRS=("src/" "include/" "tests/")
STYLE="file"
FILE_TYPES=("*.c" "*.h" "*.cpp" "*.hpp" "*.cc" "*.hh" "*.cxx" "*.hxx" "*.inc" "*.inl")
CHECK_MODE=false

# Arguments
if [[ "$1" == "--check" ]]; then
    CHECK_MODE=true
    echo "Verification mode"
fi

# Check clang-format
if ! command -v clang-format >/dev/null 2>&1; then
    echo -e "\033[31mError: clang-format is not installed.\033[0m"
    exit 1
fi

# Process files
for dir in "${SOURCE_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        for pattern in "${FILE_TYPES[@]}"; do
            while IFS= read -r -d '' file; do
                if [[ "$CHECK_MODE" == true ]]; then
                    if ! clang-format --style="$STYLE" --Werror --dry-run "$file" >/dev/null 2>&1; then
                        echo -e "\033[31m✖ Incorrect format: $file\033[0m"
                        exit 1
                    fi
                else
                    echo -e "\033[34mFormatting: $file\033[0m"
                    clang-format -i --style="$STYLE" "$file"
                fi
            done < <(find "$dir" -type f -iname "$pattern" -print0)
        done
    fi
done

echo -e "\033[32m✓ Operation completed.\033[0m"
