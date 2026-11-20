@echo off

if not exist compile_flags.windows.txt exit /b 1
if not exist compile_flags.txt exit /b 1

ECHO.[33m____swapping_to_win_mode_____[0m

MOVE /-Y compile_flags.txt compile_flags.linux.txt> NUL 2> NUL
MOVE /-Y compile_flags.windows.txt compile_flags.txt> NUL 2> NUL

ECHO.[33m____finished_________________[0m
