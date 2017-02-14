#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include "util.h"

/*
 * Read a line from stdin.
 */
char *sh_read_line(void)
{
    char *line = NULL;
    ssize_t bufsize = 0;
    
    getline(&line, &bufsize, stdin);
    return line;
}

/*
 * Do stuff with the shell input here.
 */
int sh_handle_input(char *line, int fd_toserver)
{
    /* Check for \seg command and create segfault */
    if(!strncmp(line, CMD_SEG, strlen(CMD_SEG))){
		char *buf = NULL;
		*buf = "c";
	}
    
    return 0;
}

/*
 * Check if the line is empty (no data; just spaces or return)
 */
int is_empty(char *line)
{
    while (*line != '\0') {
        if (!isspace(*line))
            return 0;
        line++;
    }
    return 1;
}

/*
 * Start the main shell loop:
 * Print prompt, read user input, handle it.
 */
void sh_start(char *name, int fd_toserver)
{
    char *line = NULL;
	
	// start a loop to print prompt.
    while(1){
        print_prompt(name);
        line = sh_read_line();
        // skip the empty lines.
        if(is_empty(line)){  									
            continue;
        }
        else{
			sh_handle_input(line, fd_toserver); 						// write message to server for processing.
            if (write(fd_toserver, line, MSG_SIZE) < 0)
                perror("writing to server");
        }
    }
    
}

int main(int argc, char **argv)
{
    /* Extract pipe descriptors and name from argv */
    /* Fork a child to read from the pipe continuously */
    user_chat_box_t shel;
    shel.child_pid = 0;
    if((shel.child_pid = fork()) == -1)
		perror("failed to fork shell's child");
    
    // parse shell's name & file desciptors for pipes.
    strcpy(shel.name, argv[3]);
    shel.ptoc[0] = atoi(argv[1]);
    shel.ctop[1] = atoi(argv[2]);
    
    char *childID = (char *)malloc(MSG_SIZE*sizeof(char));
    char *buf = (char *)malloc(MSG_SIZE*sizeof(char));
    
    // shell reads from the server.
    if(shel.child_pid == 0)
    {
        while(1)
        {
            if(read(shel.ptoc[0], buf, MSG_SIZE) >= 0){
                printf("\n%s", buf);
                fflush(stdout);
            }
            
            // set a time interval with 1000 ms.
            usleep(1000);
        }
    }
    
    else{
        while(1){
			sprintf(childID, "%s %ld", "\\child_pid", shel.child_pid);	
			// shell's child writes to server.
            if(write(shel.ctop[1], childID, strlen(childID)+1) == -1)
				perror("failed to write to the server");
            sh_start(shel.name, shel.ctop[1]);  //dead loop
            
        }
    }
    /*
     * Once inside the child
     * look for new data from server every 1000 usecs and print it
     */ 
    
    /* Inside the parent
     * Send the child's pid to the server for later cleanup
     * Start the main shell loop
     */
}
