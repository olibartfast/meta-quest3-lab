#!/usr/bin/env sh

# Reuse the checked-in Gradle 8.5 wrapper from the preserved legacy sample.
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$script_dir/XrPassthrough/Projects/Android/gradlew" -p "$script_dir" "$@"
