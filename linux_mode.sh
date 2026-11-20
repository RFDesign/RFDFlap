#!/bin/bash

[ -f "compile_flags.linux.txt" ] || exit 1
[ -f "compile_flags.txt" ] || exit 1

YELLOW='\033[1;33m'

echo -e "${YELLOW}____swapping_to_linux_mode___${RESET}"

mv compile_flags.txt compile_flags.windows.txt;
mv compile_flags.linux.txt compile_flags.txt;

echo -e "${YELLOW}____finished_________________${RESET}"
