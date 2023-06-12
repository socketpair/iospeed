#!/bin/bash

set -e -u

find . -type f -not -path './cmake-build*' \( -iname '*.c' -o -iname '*.cpp' -o -iname '*.h' \) -exec clang-format -i '{}' '+'
