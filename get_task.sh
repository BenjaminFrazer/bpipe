#!/bin/bash

if [ $# -ne 2 ]; then
  echo "Usage: $0 <heading-substring> <markdown-file>"
  exit 1
fi

search="$1"
file="$2"

# Escape search string for use in regex
escaped_search=$(printf '%s\n' "$search" | sed 's/[][\/.^$*]/\\&/g')

# Find the heading and print until next heading of same or higher level
awk -v search="$escaped_search" '
BEGIN {
  found = 0
  level = 0
}
# Match headings: lines starting with one or more #
/^#+[ \t]+/ {
  cur_level = length($1)  # heading level is number of #
  heading_text = substr($0, match($0, /[ \t]+/) + 1)
  if (found && cur_level <= level) exit
  if (!found && heading_text ~ search) {
    found = 1
    level = cur_level
  }
}
found
' "$file"
