#include "sh61.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>


int inter_signal = -1; 
pid_t current_pgid = -1; 
// struct command

// with cd you don't want to fork, you don't want to execvp 
//  before running make child, check if "cd" 
        // if it is, call change dir -> chdir 
    // the exit status of chdir is the exit status of that command 

struct command {
    std::vector<std::string> args;
    pid_t pid;      // process ID running this command, -1 if none
    pid_t command_pgid = 0; // process group id 

    command();
    ~command();

    pid_t make_child(pid_t pgid);

    command* next = nullptr; 

    int status; 
    int type;
    bool run = true; 
    bool isPipe = false; 
    bool commandGroupStart = false; 
    bool claimed = false; 
    std::vector<std::string> fileName; 
    std::vector<std::string> redirectType; 

    int prevPipeRead = -1; 
};


// command::command()
//    This constructor function initializes a `command` structure. You may
//    add stuff to it as you grow the command structure.

command::command() {
    this->pid = -1;
}


// command::~command()
//    This destructor function is called to delete a command.

command::~command() {
    delete this->next; 
}

void signal_handler(int signal){
    inter_signal = signal; 
}

// COMMAND EXECUTION

// command::make_child(pgid)
//    Create a single child process running the command in `this`.
//    Sets `this->pid` to the pid of the child process and returns `this->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating an array of `char*` arguments using
//       `this->args[N].c_str()`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t command::make_child(pid_t pgid) {
    (void) pgid; 

    // how can you tell if you're on the writing end of a pipe? (left hand)
    // --> if the command has a sibling 
    int pfd[2] = {-1, -1}; 
    if(this->next != nullptr && this->next->args.size() > 0 && this->isPipe){ 
        int r = pipe(pfd); 
        assert(r >= 0); 
    }

    // with cd you don't want to fork, you don't want to execvp 
    //  before running make child, check if "cd" 
        // if it is, call change dir -> chdir 
    // the exit status of chdir is the exit status of that command 
    if(this->args[0].compare("cd") == 0){
        int r = chdir(this->args[1].c_str()); 
        if(r == -1){
            r = 1; 
        }
        this->status = r; 
    }
    else{
        pid_t child = fork(); 
        
        if(child == 0){ // child 
            if(this->commandGroupStart){
                this->command_pgid = pgid; 
                current_pgid = pgid; 
                setpgid(0, 0); 
            } else{
                setpgid(0, pgid); 
            }
            if(this->next && this->next->claimed){
                this->next->command_pgid = this->command_pgid; 
            }

            char* arr[this->args.size() + 1]; 
            for(unsigned i = 0; i < this->args.size(); i++){
                arr[i] = (char*)this->args.at(i).c_str(); 
                //printf("args: %s\n", arr[i]); 
            }
            arr[this->args.size()] = NULL; 

            // set up the child process's environment 
            // set up pipes 
            // - if this command is ont he right-hand side of a pipe, 
            //   then its standard input is that pipe. 
            if(this->prevPipeRead >= 0){
                dup2(this->prevPipeRead, STDIN_FILENO); 
                close(this->prevPipeRead); 
            }
            // - if this command is on the left-hand side of a pipe, 
            //   then its standard output is that pipe 
            if(pfd[0] >= 0){
                dup2(pfd[1] , STDOUT_FILENO); 
                close(pfd[1]); 
                close(pfd[0]); // can't have an extra end of a pipe open 
            }
            // set up redirections 

            for(unsigned i = 0; i < this->redirectType.size(); i++){
                // - If this command '> f', then its standard output is the file 'f'. 
                if(this->redirectType[i] == ">"){
                    int fd = open(this->fileName[i].c_str(), O_WRONLY | O_CREAT|  O_TRUNC | O_APPEND, 0666); 
                    if(fd == -1){
                        perror( strerror(errno)); 
                        _exit(EXIT_FAILURE); 
                    }

                    dup2(fd, STDOUT_FILENO); 
                    close(fd); 
                    
                }
                // - if this command '< f', then its standard input is the file 'f' 

                if(this->redirectType[i] == "<"){
                    int fd = open(this->fileName[i].c_str(), O_RDONLY, 0666);
                    if(fd == -1){
                        perror(strerror(errno)); 
                        _exit(EXIT_FAILURE); 
                    }

                    dup2(fd, STDIN_FILENO); 
                    close(fd); 
                }


                if(this->redirectType[i] == "2>"){
                    int fd = open(this->fileName[i].c_str(), O_CREAT | O_WRONLY | O_RDONLY, 0666); 
                    if(fd == -1){
                        perror(strerror(errno)); 
                        _exit(EXIT_FAILURE); 
                    }
                    dup2(fd, STDERR_FILENO); 
                    close(fd); 
                }
            } 
            
            int r = execvp(this->args[0].c_str(), arr); 
           
            if(r == -1){
                fprintf(stderr, "execvp failed\n"); 
                _exit(EXIT_FAILURE); 
            }

        } 
        else{
            if(this->commandGroupStart){
                this->command_pgid = child; 
                current_pgid = child; 
               // printf("parent pgid: %d\n", child);
                setpgid(child, child);
            } else{
                setpgid(child, pgid); 
            }
            if(this->next && this->next->claimed){
                this->next->command_pgid = this->command_pgid; 
            }
            if(this->prevPipeRead >= 0){
                close(this->prevPipeRead); 
            }
            if(pfd[0] >= 0){
                close(pfd[1]); 
                this->next->prevPipeRead = pfd[0]; 
            }
        }
        this->pid = child; 
    }
    return this->pid;
}


// run(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `make_child` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    PART 1: Start the single command `c` with `c->make_child(0)`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `run` (or in helper functions).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline and pass it to
//         `make_child`.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.
// if the chunk should be run in the background
bool background_chain(command* c){
    int type = c->type; 
    while(c->type != TYPE_SEQUENCE && c->type != TYPE_BACKGROUND){
        c = c->next; 
        if(!c){
            break; 
        } 
        type = c->type; 
    }
    return type == TYPE_BACKGROUND; 
}
//if the command is on the left side of a pipe
bool isPipe(command* c){
    int type = c->type; 
    while(c->type != TYPE_PIPE && c->type != TYPE_BACKGROUND && c->type != TYPE_SEQUENCE){
        c = c->next; 
        if(!c){
            break; 
        } 
        type = c->type; 
    }
    return type == TYPE_PIPE; 
}

void run(command* c) {
    pid_t newProcess = -1;  // process id of child 
    bool forked = false; 
    while(c != nullptr){
        bool checkBackground = background_chain(c);  
        if(inter_signal == SIGINT){
            inter_signal = -1; 
            if(c->next && c->type == TYPE_AND){
                c->next->status = 1; 
                c->next->run = false; 
            } 
            c = c->next; 
            continue; 
        } 
        
        if(isPipe(c)){
            c->isPipe = true; 
        }

        // if the command is supposed to be in the background...
        if(checkBackground){ 
            // if we havn't forked already (if we havn't made a subshell)...
            if(!forked){
                newProcess = fork(); 
                forked = true; 
            }
            // if we're currently in the parent...
            if(newProcess > 0){
                // if we've reached the end of the background command...
                if(c->type == TYPE_BACKGROUND){
                    forked = false; 
                }
                c = c->next;        
                continue; 
            } 
        } // otherwise...
        else if(!checkBackground){
            forked = false; 
            // if we're in the child
            if(newProcess == 0){
                _exit(0); 
            }
        }

        // if we're supposed to run the command 
        if(c->args.size() > 0 && c->run){
            if(!checkBackground){
                if(newProcess > 0){
                    claim_foreground(c->command_pgid); 
                }
            }
            pid_t a = c->make_child(c->command_pgid); 
            int status; 
            
            // if it isn't a pipe then we want to wait (the last command of the pipe will have type pipe)
            if(c->type != TYPE_PIPE){
                waitpid(a, &status, 0); 
            }
            if(!checkBackground){
                claim_foreground(0); 
            }
            // if the process exited sucessfully 
            if(WIFEXITED(status)){
                int process_status = WEXITSTATUS(status); 
                if(c->args[0].compare("cd") != 0){
                    c->status = process_status;
                }                
            }
        }

        // if the status of the current command doesn't match with the type of the conditional...
        // set the next command to not be runnable
        if((c->status == 0 && c->type == TYPE_OR )||(
            c->status == 1 && c->type == TYPE_AND)){
            c->next->status = c->status; 
            c->next->run = false; 
        }
        c = c->next; 
    }
}


// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

command* parse_line(const char* s) {
    int type;
    std::string token;

    command* c = nullptr;
    command* first = nullptr; 
    while ((s = parse_shell_token(s, &type, &token)) != nullptr) {
        if (!c) {
            c = new command;
            first = c; 
        } 

        c->type = type; 
        if(type == TYPE_BACKGROUND || type == TYPE_SEQUENCE || type == TYPE_OR || 
            type == TYPE_AND || type == TYPE_PIPE || type == TYPE_REDIRECTION){  

            if(type == TYPE_REDIRECTION){
                c->redirectType.push_back( token.c_str()); 
                s = parse_shell_token(s, &type, &token); 
                c->fileName.push_back( token.c_str()); 
                continue; 
            } 
            if(!c->claimed){
                c->commandGroupStart = true; 
            }
            if(!c->next){
                c->next = new command; 
                if(type == TYPE_PIPE){
                    c->next->claimed = true; 
                }
                c = c->next; 
            }      
        }         
        else{
            c->args.push_back(token); 
        }
    }
    if(c && !c->claimed){
        c->commandGroupStart = true; 
    }
    return first;
}


int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    bool quiet = false;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            exit(1);
        }
    }
    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);
    set_signal_handler(SIGINT, signal_handler);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            int textColor = rand() % 7 + 30; 
            int backgroundColor = rand() % 7 + 40; 
            printf("\033[1;3;5;4;23;51;%d;%dm\u0405\u04AD\u04D6\u0475\u04D6[%d]$\033[0m ", textColor, backgroundColor, getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            if (command* c = parse_line(buf)) {
                run(c);
                delete c;
            }
            bufpos = 0;
            needprompt = 1;
        }
        // Handle zombie processes and/or interrupt requests

        if(inter_signal == SIGINT){
            printf(" \n"); 
            needprompt = true; 
            inter_signal = -1; 
        }

        while(waitpid(-1, 0, WNOHANG) > 0){
            _exit(0); 
        }
    }

    return 0;
}
