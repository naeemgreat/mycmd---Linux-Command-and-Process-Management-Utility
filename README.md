Implemented Commands:

mycmd top

Displays system information including average CPU load, total processes, and active processes.
Periodically updates at 10s intervals until interrupted with 'q'.
Lists up to 20 running processes, ordered by PID.
mycmd cmd arg1 ... argn

Executes the specified Linux command (cmd) with the provided arguments.
mycmd cmd arg1 ... argn “>” file

Executes the Linux command, redirecting the output to the specified file.
mycmd cmd arg1 ... argn “<” file

Executes the Linux command, taking input from the specified file.
mycmd cmd1 arg1 ... argn “|” cmd2 arg1 ... argn

Executes Linux commands in a pipeline, redirecting the output of cmd1 to the input of cmd2.

Usage:
./mycmd [command] [arguments/options]
