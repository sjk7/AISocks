#!/usr/bin/env python3
"""Replace printf("DEBUG...") with DLOG() and remove fflush(stdout) in test files."""
import re
import sys

MACRO = """\n// Enable diagnostic output by compiling with -DTEST_VERBOSE.
#ifdef TEST_VERBOSE
#  define DLOG(...) do { printf(__VA_ARGS__); fflush(stdout); } while(0)
#else
#  define DLOG(...) do {} while(0)
#endif

"""

def process_file(path):
    with open(path, 'r') as f:
        lines = f.readlines()

    # Check if DLOG macro already present
    if any('define DLOG' in l for l in lines):
        print(f"{path}: DLOG already present, skipping macro insertion")
    else:
        # Insert macro after last #include line
        last_inc = -1
        for i, line in enumerate(lines):
            if line.startswith('#include'):
                last_inc = i
        if last_inc >= 0:
            lines = lines[:last_inc+1] + [MACRO] + lines[last_inc+1:]

    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip()
        # Match printf( where format string is a DEBUG trace
        if re.match(r'printf\s*\(\s*"(DEBUG|debug|\[DEBUG)', stripped):
            new_line = line.replace('printf(', 'DLOG(', 1)
            result.append(new_line)
            i += 1
            # Collect continuation lines until statement ends with );
            if not new_line.rstrip('\n').rstrip().endswith(');'):
                while i < len(lines):
                    cont = lines[i]
                    result.append(cont)
                    i += 1
                    if cont.rstrip('\n').rstrip().endswith(');'):
                        break
            # Skip immediately-following fflush(stdout);
            if i < len(lines) and lines[i].strip() == 'fflush(stdout);':
                i += 1
        else:
            result.append(line)
            i += 1

    # Remove any remaining standalone fflush(stdout); lines
    result2 = [line for line in result if line.strip() != 'fflush(stdout);']

    with open(path, 'w') as f:
        f.writelines(result2)

    fflush_remaining = sum(1 for l in result2 if 'fflush' in l and 'DLOG' not in l)
    dlog_count = sum(1 for l in result2 if 'DLOG(' in l and 'define' not in l)
    print(f"{path}: {len(lines)}->{len(result2)} lines, {dlog_count} DLOG calls, {fflush_remaining} fflush remaining")

for path in sys.argv[1:]:
    process_file(path)
