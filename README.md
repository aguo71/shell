# Implementation of Unix Shell

### Structure of shell-1
My shell is structured such that all my code is stored in sh.c. The main function is responsible for the REPL command line, and calling the functions necessary to both parse and execute the user input. The first function it calls is parse. Parse is responsible for filling an "argv" array containing all the tokens in buffer, as well as saving the input/output file if specified, and taking note if >> is used instead of >. Now after parsing, main will check if the first token in argv is a built-in command, and if so, will call the appropriate function which handles the built-in functionality. If not, main will finally call the execute function because argv must only contain an executable and its associated parameters/input/output redirects at this point. Execute is responsible for forking a child process and handling input/output redirects before finally calling execv. The parent execute function will wait until this is complete before returning to main. Main then checks for errors and continues to a new iteration of the REPL while loop. Additional comments are in sh.c explaining function logic.

### How to compile 
The command:' make all ' will compile the executables 33sh and 33noprompt, which are sh.c compiled with or without the PROMPT flag respectively. The makefile is also capable of ' make clean all ', or ' make test ' (which runs the test suite). 

# shell-2
In addition to the features of shell 1, some changes to the shell include:

1. A small addition to parse which now takes in an extra argument background 
which represents a boolean detecting if & is the last character of the input.

2. Global variables representing a job_list and a counter for jobIDs. 

3. Main was updated to ignore certain signals and check for builtins such as 
jobs, fg, and bg. If found, main would call the respective helper functions. 
The jobs helper function simply checks for correct arguments and prints the 
jobs_list. The fg function checks for correct arguments, then parses the input 
for the jobID to resume in the foreground, transfers control of terminal to that 
process and sends a SIGCONT signal to it, then waits until its status changes 
and updates job_list accordingly. The bg function simply checks for correct 
arguments, parses input for jobID to resume in background, and sends SIGCONT 
signal to that process group.

4. Also, reaping of the job_list was added to the beginning of the main REPL 
(as well as the reaping done in execute if process was ran in foreground) 
by iterating through each job in the job_list and checking the corresponding 
waitpid status for changes and prints/updates job_list accordingly.

5. Lastly, execute was updated to take in the additional background boolean 
argument. It also resets signals to default handlers in the child process, 
sets child pgid as its pid, and transfers terminal control to it if it was 
ran in the foreground. The parent execute function also was updated to wait 
for this child process only if it was ran in the foreground, prints out the 
appropriate status change message, and transfers control back to terminal before 
returning to main; otherwise it simply adds the background process to the job_list 
and continues to main.
