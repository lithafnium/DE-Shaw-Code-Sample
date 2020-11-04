# DE-Shaw-Code-Sample

This code sample was written for CS61: Systems Programing and Machine Organization. The project description was aimed at implementing a shell,
being able to run commands as a user inputs them into the command prompt. 

Function 'make_child' forks a new child process and runs the command using the 'execvp' system call. The 'run' function runs the command list starting at command 'c',
calling make_child and waiting for the process to end using 'waitpid'. 

In order to handle background commands, command lists, conditionals, pipings, and redirects, I implemented a linkedlist to handle all the inputs from the command prompt. 
A sequence of commands and its corresponding symbols each correspond to a single node, defined at the top as 'struct command.' The code to handle 
the linkedlist is in the 'parse_line' token, where I check for the symbol in the input string whether it corresponds to a '|' for pipng, '&&' or '||' for conditionals, 
'>' or '<' for redirects, or anything else that I have to account for. 

Command lists are handled using fork(), which creates a process that runs in the background, the implementation of which is on line 278 inside the 'run' function. 

Piping is handled the make_child function on lines 129-139. It sets up the child's process environment by setting the file descriptors for the read and write ends of the pipe. dup2 simply 
connects the file descriptors to the proper value. The process can be summarised as such: 
Assume pfd is an array of length 2 that describes the file descriptors for the pipe. 
1. Create a pipe using pipe()
2. fork() of a child process 
3. Close pfd[0] in the child process 
4. Connect pfd[1] to STDOUT_FILENO using dup2()
5. close pfd[1]
6. run the command using execv() in the child 
7. (back to the parent), close pfd[1]
8. Fork off another child process b 
9. connect pfd[0] to STDIN_FILENO using dup2() 
10. close pfd[0]
11. Run b using execv() in the child 
12. (back in the parent) close pfd[0]


Redirects are handled in lines 142-178, and like with piping, I had to handle the proper file descriptors by closing or changing the read and write ends. In addition, 
basd on the type of redirect, iether >, < or 2>, each file descriptor had to be created with certain flags, whether it was write only, had to be created, truncated, or read only. 

Finally, interrupts are handled by checking a signal handler, SIGINT, down at line 455. Cd is also handled at the top of make_child, where instead of forking a new process, 
it runs the system call chdir(). 
