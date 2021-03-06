#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

int stdioCopy[2];                               //required for pipe execution to save initial IO decriptors as globals

typedef char * word;
struct cmnd {                                   //for storing string, its length and link to next
    word wrd;
    int len;
    struct cmnd * next;
};
typedef struct cmnd * cmndPtr;

struct decomposed {                             //for storing command with all necessary flags
    cmndPtr args;
    int backgr, inred, outredbeg, outredend, pipelnout, pipelnin, argsNum;
    cmndPtr redin, redout;
    struct decomposed * next;
};
typedef struct decomposed * decomPointer;

void skipSpaces (char * c) {                    //do getchar until non-space met
    while (*c == ' ') {
        *c = getchar ();
    }
    return;
}

void skipToNewLine (char * c) {
    while (*c != '\n') {
        *c = getchar ();
    }
    return;
}

cmndPtr getWord (char * c) {                    //input a single word and put it in cmnd
    int len = 0;
    int c1;
    int allocSize = 5;
    int doquote = 0;
    int quote = 0;
    word str = (word) malloc (sizeof(char) * allocSize);
    str[0] = 0;
    word tmp;
    skipSpaces (c);
    if (*c == '\n') {
        free (str);
        return NULL;        //word is empty, nothing to read
    }
    /**
        Word is being read until special symbols met or:
        While in double/single quotes - wait for the second quote
    **/
    while (quote || doquote || (*c != ' ' && *c != '\n'  && *c != '|'  && *c != '&' && *c != '>' && *c != '<')) {
        if (len >= allocSize - 1) {                     //memory reallocation if needed
            allocSize += allocSize;
            tmp = (word) malloc (sizeof(char) * allocSize);
            strcpy (tmp, str);
            free (str);
            str = tmp;
            tmp = NULL;
        }

        if (*c == '"' && !quote) {
            doquote = !doquote;
            *c = getchar ();
            continue;
        } else if (*c == "'"[0] && !doquote) {
            quote = !quote;
            *c = getchar ();
            continue;
        }

        if (*c == '\\') {
            *c = getchar ();
            if (*c == '\n') {
                printf ("> ");
                *c = getchar ();
                skipSpaces (c);
                continue;
            } else if ((*c == "\""[0] || *c == "\\"[0]) && doquote) {
                str[len] = *c;
                str[len + 1] = 0;
                len += 1;
                *c = getchar ();
                continue;
            } else {
                str[len] = "\\"[0];
                str[len + 1] = 0;
                len += 1;
                continue;
            }
        }

        str[len] = *c;
        str[len + 1] = 0;
        len += 1;
        *c = getchar();
    }
    allocSize = len + 1;
    tmp = (word) malloc (sizeof(char) * allocSize);   //reallocate once more not to consume free space
    strcpy (tmp, str);
    free (str);
    str = tmp;
    tmp = NULL;

    cmndPtr ret;                                    //create a returnable struct cmnd
    ret = (cmndPtr) malloc (sizeof(struct cmnd));;
    ret->wrd = str;
    ret->len = len;
    ret->next = NULL;
    if (len == 0) {                                 //if word was empty - return NULL and free memory
        free (ret->wrd);
        free (ret);
        return NULL;
    }
    return ret;
}

void freeCmnd (cmndPtr * beg) {                 //free unused struct cmnd
    cmndPtr curr1, curr2;
    curr1 = *beg;
    while (curr1 != NULL) {
        curr2 = curr1->next;
        free(curr1->wrd);
        free(curr1);
        curr1 = curr2;
    }
    *beg = NULL;
    return;
}

void freeMem (decomPointer * beg) {             //free unused decomposed command
    decomPointer curr1, curr2;
    curr1 = * beg;
    while (curr1 != NULL) {
        curr2 = curr1->next;
        if (curr1->args != NULL) {
            freeCmnd(&curr1->args);             //free substructure
        }
        free (curr1->redin);
        free (curr1->redout);
        free (curr1);
        curr1 = curr2;
    }
    *beg = NULL;
    return;

}

decomPointer initCommand () {                   //initialize template for decomposed command with default values
    decomPointer ret  = (decomPointer) malloc (sizeof(struct decomposed));
    ret->args = NULL;
    ret->redin = NULL;
    ret->redout = NULL;
    ret->next = NULL;
    ret->backgr = 0;
    ret->inred = 0;
    ret->outredbeg = 0;
    ret->outredend = 0;
    ret->pipelnout = 0;
    ret->pipelnin = 0;
    ret->argsNum = 0;
}

/**
    Record a struct decomposed - input command, analyze flags,
    start a next record if pipeline '|' found, input filename for IO redirect if found
**/

decomPointer getCommand (char * c) {
    decomPointer begin = initCommand ();
    int redirected = 0;
    decomPointer curr = begin;
    curr->argsNum = 0;
    cmndPtr currArg, tmp;
    curr->args = (cmndPtr) malloc (sizeof(struct cmnd));
    currArg = curr->args;
    skipSpaces (c);
    if (*c == '|' || *c == '&' || *c == '>' || *c == '<') {
        freeMem (&begin);
        printf ("%s%c\n", "Error: no command stated before ", *c);
        return NULL;
    }

    while ((*c != '\n') && (currArg != NULL)) {
        skipSpaces (c);

        /**
            Special symbols analysis - setting flags values
        **/

        if (*c == '|') {
            curr->pipelnout = 1;
            *c = getchar ();
            skipSpaces (c);
            if (*c == '\n') {
                printf("Wrong argument: | must be followed by command \n");
                freeMem (&begin);
                return NULL;
            }
            curr->next = getCommand (c);
            if (curr->next == NULL) {
                printf("Wrong argument: | must be followed by command \n");
                freeMem (&begin);
                return NULL;
            } else {
                curr->backgr = curr->next->backgr;
            }
            curr->next->pipelnin = 1;
            break;
        }
        else if (*c == '&') {
            curr->backgr = 1;
            *c = getchar ();
            skipSpaces (c);
            if (*c != '\n') {
                printf("Wrong argument: & must be at the end ");
                freeCmnd (&begin->args);
                freeMem (&begin);
                return NULL;
            }
            break;
        }
        else if (*c == '>') {
            if (curr->outredend || curr->outredbeg) {
                printf ("%s\n", "Error: double output redirect");
                freeCmnd (&begin->args);
                freeMem (&begin);
                return NULL;
            }
            *c = getchar ();
            if (*c == '>') {
                curr->outredend = 1;
                *c = getchar ();
            } else {
                curr->outredbeg = 1;
            }
            curr->redout = getWord(c);
            if (curr->redout == NULL) {
                printf("Output redirect error: No filename stated\n");
                freeCmnd (&begin->args);
                freeMem (&begin);
                return NULL;
            }
            redirected = 1;
            continue;
        }
        else if (*c == '<') {
            if (curr->inred) {
                printf ("%s\n", "Error: double input redirect");
                freeCmnd (&begin->args);
                freeMem (&begin);
                return NULL;
            }
            *c= getchar ();
            curr->inred = 1;
            curr->redin = getWord(c);
            if (curr->redin == NULL) {
                printf("Input redirect error: No filename stated\n");
                freeMem (&begin);
                return NULL;
            }
            redirected = 1;
            continue;
        }
        skipSpaces (c);
        currArg->next = getWord (c);
        // nothing must follow IO redirect filenames except special symbols
        if (currArg->next != NULL && (curr->inred || curr->outredbeg || curr->outredend)) {
            printf("Input error: IO redirect must be at end\n");
            freeMem (&begin);
            return NULL;
        }
        currArg = currArg->next;
    }

    tmp = curr->args;
    curr->args = curr->args->next;
    free (tmp);
    skipToNewLine (c);
    if ((begin->inred && begin->pipelnin) || ((begin->outredbeg || begin->outredend) && begin->pipelnout)) {
        printf("Stdio redirect and pipeline conflict\n");
        freeMem (&begin);
        return NULL;
    }else if (begin->args == NULL) {
        freeMem (&begin);
        return NULL;
    } else {
        return begin;
    }
}

word * parseCmnd (decomPointer toParse) {       //make a char ** from decomposed for execvp call
    int argsNum = 0;
    cmndPtr curr = toParse->args;

    while (curr != NULL) {
        argsNum ++;
        curr = curr->next;
    }

    word * num = (word *) malloc (sizeof(word *) * (argsNum + 1));
    curr = toParse->args;

    for (int i = 0; i <= argsNum; i++) {
        if (i < argsNum) {
            num[i] = curr->wrd;
            curr = curr->next;
        } else {
            num[i] = NULL;
        }
    }

    return num;
}

void execute (decomPointer begin) {             //main magic here, fork/exec called here
    word* argv = parseCmnd (begin);
    int fd[2];
    int ifile, ofile;
    ifile = 0;
    ofile = 0;

    if (pipe(fd) == -1) {                       //if pipe failed to open
        printf("Error: %s\n", strerror(errno));
        free (argv);
        return;
    }

    pid_t p = fork ();

    if (p == -1) {
        printf("Error: %s\n", strerror(errno));
        free (argv);
        return;
    }
    else if (p == 0) {
        //  child process
        close (fd[0]);
        dup2 (fd[1], 1);
        /**
            Check for redirect flags, replace stdIO to file IO
        **/
        if (begin->inred == 1) {
            ifile = open (begin->redin->wrd, O_RDONLY);
            if (ifile == -1) {
                printf("Error: %s\n", strerror(errno));
                exit (EXIT_FAILURE);
            }
            dup2 (ifile, 0);
        }
        if (begin->outredbeg == 1) {
            ofile = open (begin->redout->wrd, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, S_IRUSR | S_IWUSR);
            if (ofile == -1) {
                printf("Error: %s\n", strerror(errno));
                exit (EXIT_FAILURE);
            }
            dup2 (ofile, 1);
        } else if (begin->outredend == 1) {
            ofile = open (begin->redout->wrd, O_RDWR | O_APPEND | O_SYNC);
            if (ofile == -1) {
                printf("Error: %s\n", strerror(errno));
                exit (EXIT_FAILURE);
            }
            dup2 (ofile, 1);
        }
        execvp (argv[0], argv);
        //in case of error:
        dup2 (stdioCopy[0], 0);                 //restore default IO descriptors and show error, exit with failure
        dup2 (stdioCopy[1], 1);
        printf("Error: %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }
    else {
        //  parent process
        int stat, fild;
        char buf;
        close (fd[1]);
        if (begin->pipelnout && begin->next != NULL) {
            /**
                Found a pipeline output,
                redirect execvp output to recursively called execute ()
            **/
            dup2 (fd[0], 0);
            execute (begin->next);
            dup2 (stdioCopy[0], 0);
            dup2 (stdioCopy[1], 1);
        } else {
            dup2 (stdioCopy[1], 1);
            //restore stdio
            while (fild = read (fd[0], &buf, sizeof(char)) > 0) {
                write (1,&buf,1);
            }
        }
        waitpid (p, &stat, 0);

    }
    close(fd[0]);
    free (argv);
    if (ifile > 0){
        close (ifile);
    }
    return;
}

int main () {                                   //a bit too large, but simple
    char c;
    const char * exitstr = "exit";              //needed for exit/cd commands recognition
    const char * cdstr = "cd";
    pid_t pdt, pid;
    int backgrounds = 0;
    stdioCopy[0] = dup(0);
    stdioCopy[1] = dup(1);
    int exitflag, chdirflag;
    decomPointer curr;
    word cwd = (word) malloc (PATH_MAX * sizeof (char));
    if (getcwd(cwd, sizeof(char) * PATH_MAX) == NULL) {
        perror("getcwd() error");
    }
    else {
        printf("%s$ ", cwd);
    }
    int tmp;
    tmp = getchar ();
    if (tmp == EOF) {
        exitflag = 0;
        printf("\n");
    } else {
        c = (char) tmp;
        curr = getCommand (&c);
        printf ("\n");
        if (curr != NULL) {
            exitflag = strcmp (curr->args->wrd, exitstr);
        } else {
            exitflag = 1;
        }
    }

    while (exitflag != 0) {                     //while entered command != exit
        pid = waitpid(-1, NULL, WNOHANG);
        if (pid > 0) {
            backgrounds--;
        }
        if (curr != NULL) {                     //check if command not empty
            if (strcmp (curr->args->wrd, cdstr) == 0) {
                if (curr->args->next == NULL) {
                    printf ("Error: no path stated\n");
                } else if (chdirflag = chdir (curr->args->next->wrd) == -1) {
                    printf("Error: %s\n", strerror(errno));
                }
            } else {
                if (curr->backgr == 1) {
                    backgrounds ++;
                    pdt = fork ();
                    if (pdt == 0) {             //background mode needed, launching one more process
                        execute (curr);
                        exit (1);
                    }
                } else {
                    printf ("\n");
                    execute (curr);
                    printf ("\n");
                }
            }
            freeMem (&curr);
        }

        skipToNewLine (&c);
        if (getcwd(cwd, sizeof(char) * PATH_MAX) == NULL) {
            perror("getcwd() error");
        }
        else {
            printf("%s$ ", cwd);
        }
        tmp = getchar ();
        if (tmp == EOF) {
            exitflag = 0;
            printf("\n");
        } else {
            c = (char) tmp;
            curr = getCommand (&c);
            printf("\n");
            if (curr != NULL) {
                exitflag = strcmp (curr->args->wrd, exitstr);
            } else {
                exitflag = 1;

            }
        }

    }
    pid = waitpid(-1, NULL, WNOHANG);                   //check if all children terminated, wait for them
    if (pid > 0) {
        backgrounds--;
    }
    if (backgrounds > 0) {
        printf ("Waiting for all children to terminate, CTRL - C to quit now\n");
    }

    while (backgrounds > 0) {
        pid = waitpid(-1, NULL, WNOHANG);
        if (pid > 0) {
            backgrounds--;
        }
    }
    free (cwd);
    freeMem (&curr);

    return 0;
}
