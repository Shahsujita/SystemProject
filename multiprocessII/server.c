/* CSci4061 S2016 Assignment 2
 * section: 7
 * section: 2
 * section: 2
 * date: 03/10/2016
 * name: Sujita Shah, Charon chen, Cheng Chen
 * id: shahx220, chen4393, chen4162 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include "util.h"

/*
 * Identify the command used at the shell
 */

int parse_command(char *buf)
{
    int cmd;
    
    if (starts_with(buf, CMD_CHILD_PID))
        cmd = CHILD_PID;
    else if (starts_with(buf, CMD_P2P))
        cmd = P2P;
    else if (starts_with(buf, CMD_LIST_USERS))
        cmd = LIST_USERS;
    else if (starts_with(buf, CMD_ADD_USER))
        cmd = ADD_USER;
    else if (starts_with(buf, CMD_EXIT))
        cmd = EXIT;
    else if (starts_with(buf, CMD_KICK))
        cmd = KICK;
    else
        cmd = BROADCAST;
    
    return cmd;
}

/*
 * Utility function.
 * Given a command's input buffer, extract name.
 */
char *extract_name(int cmd, char *buf)
{
    char *s = NULL;
    
    s = strtok(buf, " ");
    s = strtok(NULL, " ");
    if (cmd == P2P)
        return s;	/* s points to the name as no newline after name in P2P */
    s = strtok(s, "\n");	/* other commands have newline after name, so remove it*/
    return s;
}

/*
 * List the existing users on the server shell
 */
int list_users(user_chat_box_t *users, int fd)
{
    /***** Insert YOUR code *******/
    
    char listnames[MAX_USERS][MSG_SIZE];
    char* buffer = (char* )malloc(MSG_SIZE);
    int idx, count = 0, byteswritten;
    
    for (idx =0 ; idx < MAX_USERS ; idx++){
        
        if(users[idx].status == SLOT_FULL){
            count++;
            strcpy(listnames[idx], users[idx].name);
        }
    }
    
    if(count == 0)
        printf("<no users>\n");
    else {
        //Construct a list of user names, cyclic traverse
        for (idx = 0; idx < MAX_USERS; idx++) {
            if (users[idx].status == SLOT_FULL) {
                char word[MSG_SIZE];
                sprintf(word, "%s%s", listnames[idx], "\n");
                strcat(buffer, word);
            }
        }
    }
    
  
    //send the list to the requester!
    if((byteswritten = write(fd, buffer, MSG_SIZE)) == -1){
		perror("write failed(list_users)\n");
	}
    
    free(buffer);
    return 0;
}

/*
 * Add a new user
 */
int add_user(user_chat_box_t *users, char *buf, int server_fd)
{
	int i,f1,f2;
    char* buf_cp = buf;
    char* name;
    
    name = extract_name(ADD_USER, buf_cp);
    
    //adding user if non empty and setting the status to be full 
    for(i = 0; i < MAX_USERS; i++)                                     
    {
        if (users[i].status == SLOT_EMPTY) {
            strcpy(users[i].name, name);
            users[i].status = SLOT_FULL;
            break;
        }
    }
    
    if(i == MAX_USERS){                                                 //check for the limit
        printf("No empty slot, user limit exceed\n");
        return -1;
    }
   
    char message[MSG_SIZE];
    
    sprintf(message, "%s%s%s", "Adding user " , name, " ...\n");
    
    int nwrite;
    nwrite = write(server_fd, message, MSG_SIZE);

    if(nwrite < 0)                                                      //notify server's shell
        perror("writing to server shell");
    
    if(pipe(users[i].ptoc) == -1) {                                     
        perror("Failed to creat the pipe of user");
        return -1;
    }
    if(pipe(users[i].ctop) == -1) {
        perror("Failed to creat the pipe of user");
        return -1;
    }
    
    //make pipe nonblocking 
    if((f1 = fcntl (users[i].ctop[0], F_GETFL, 0)) == -1)               
		return -1;
    if((f2 = fcntl (users[i].ptoc[0], F_GETFL, 0)) == -1)
		return -1;
    if(fcntl (users[i].ctop[0], F_SETFL, f1 | O_NONBLOCK) == -1)       
		return -1;
    if(fcntl (users[i].ptoc[0], F_SETFL, f2 | O_NONBLOCK) == -1)
		return -1;
    
    return i;
}

/*
 * Broadcast message to all users. Completed for you as a guide to help with other commands :-).
 */
int broadcast_msg(user_chat_box_t *users, char *buf, int fd, char *sender)
{
    int i;
    const char *msg = "Broadcasting...", *s;
    char text[MSG_SIZE];
    
    /* Notify on server shell */
    if (write(fd, msg, strlen(msg) + 1) < 0)
        perror("writing to server shell");
    
    /* Send the message to all user shells */
    s = strtok(buf, "\n");
    sprintf(text, "%s : %s", sender, s);
    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_EMPTY)
            continue;
        if (write(users[i].ptoc[1], text, strlen(text) + 1) < 0)
            perror("write to child shell failed");
    }
}

/*
 * Close all pipes for this user
 */
void close_pipes(int idx, user_chat_box_t *users)
{
    if(close(users[idx].ctop[0]) == -1)                                 //close the pipes to cleanup
		perror("failed to close the ctop[0]");
    if(close(users[idx].ctop[1]) == -1)
        perror("failed to close the ctop[1]");
    if(close(users[idx].ptoc[0]) == -1)
        perror("failed to close the ptoc[0]");
    if(close(users[idx].ptoc[1]) == -1)
        perror("failed to close the ptoc[1]");
}

/*
 * Cleanup single user: close all pipes, kill user's child process, kill user
 * xterm process, free-up slot.
 * Remember to wait for the appropriate processes here!
 */
void cleanup_user(int idx, user_chat_box_t *users)
{
    close_pipes(idx, users);
   
    if(users[idx].status == SLOT_FULL)
        users[idx].status = SLOT_EMPTY;
    
    kill(users[idx].child_pid, 9);                                      //kill users xterm window and its child
    waitpid(users[idx].child_pid, NULL, 0);                             //wait for the user's child, users
    kill(users[idx].pid, 9);
	waitpid(users[idx].pid, NULL, 0);
}

/*
 * Cleanup all users: given to you
 */
void cleanup_users(user_chat_box_t *users)
{
    int i;
    
    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_EMPTY)
            continue;
        cleanup_user(i, users);
    }
}

/*
 * Cleanup server process: close all pipes, kill the parent process and its
 * children.
 * Remember to wait for the appropriate processes here!
 */
void cleanup_server(server_ctrl_t server_ctrl)
{
	if(close(server_ctrl.ptoc[0]) == -1)								//close the pipes of server to cleanup
		perror("failed to close the server_ctrl.ptoc[0]");
	if(close(server_ctrl.ctop[0]) == -1)
		perror("failed to close the server_ctrl.ctop[0]");
	if(close(server_ctrl.ptoc[1]) == -1)
		perror("failed to close the server_ctrl.ptoc[1]");
	if(close(server_ctrl.ctop[1]) == -1)
		perror("failed to close the server_ctrl.ctop[1]");
	
	kill(server_ctrl.child_pid, 9);                                     //kill the server and its shell
    waitpid(server_ctrl.child_pid, NULL, 0);                            //wait for server and its shell
    
    kill(server_ctrl.pid, 9);
    waitpid(server_ctrl.pid, NULL, 0);	
}

/*
 * Utility function.
 * Find user index for given user name.
 */
int find_user_index(user_chat_box_t *users, char *name)
{
    int i, user_idx = -1;
    
    if (name == NULL) {                                                 
        fprintf(stderr, "NULL name passed.\n");
        return user_idx;
    }
    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_EMPTY)
            continue;
        if (strncmp(users[i].name, name, strlen(name)) == 0) {
            user_idx = i;
            break;
        }
    }
    
    return user_idx;
}


/*
 * Send personal message. Print error on the user shell if user not found.
 */
void send_p2p_msg(int idx, user_chat_box_t *users, char *buf)
{
    /* get the target user by name (hint: call (extract_name() and send message */
    
    char *buf4 = (char *)malloc(MSG_SIZE * sizeof(char));						// allocate a buffer to store the command.
    char *nametok;														// for parsing user's name.
    
    strcpy(buf4, buf);													// store the command.
    nametok = strtok(buf, " ");											
    nametok = strtok(NULL, " ");										// parse user's name.
    
    int target_idx = find_user_index(users, nametok);					// get the target_user's index
    int byteswritten;
 
    if(target_idx < 0)  												// if the target user's index is invalid, then print the error message.
    {
        char* errorbuf = "User not found\n";
        if((byteswritten = write(users[idx].ptoc[1], errorbuf, strlen(errorbuf) + 1)) == -1) // test the returned value of "write" function 
			perror("failed to write to users[idx] in send_p2p_msg");			
    }
    else {
        buf4 = buf4 + strlen(nametok) + strlen(CMD_P2P) + 2;			// copy the command after <username>.
        char* buf5 = (char *)malloc(MSG_SIZE);								// allocate a buffer to print prompt.
        sprintf(buf5, "%s : %s", users[idx].name, buf4);				// combine to get a <name> : <cmd> format
        if(write(users[target_idx].ptoc[1], buf5, strlen(buf5) + 1) == -1) // test the returned value of "write" function 
			perror("failed to write to users[target_idx] in send_p2p_msg");
    }
}

int main(int argc, char **argv)
{ 
    /* open non-blocking bi-directional pipes for communication with server shell */
    
    server_ctrl_t serv;                                                 // define the server object.
    user_chat_box_t users[MAX_USERS];									// define the user's object array.
    int idx;
    for(idx = 0; idx < MAX_USERS; idx++) {
        users[idx].status = SLOT_EMPTY;									// initialization.
    }
    
    int flag1, flag2;                                                   // create a pipe for IPC.
    
    if(pipe(serv.ctop)==-1){
        perror("failed to create the pipe");
        return 1;
    }
    
    if(pipe(serv.ptoc)==-1){
        perror("failed to create the pipe");
        return 1;
    }
    
    if((flag1 = fcntl (serv.ctop[0], F_GETFL, 0)) == -1)				// non-blocking.
		return -1;
    if((flag2 = fcntl (serv.ptoc[0], F_GETFL, 0)) == -1)
		return -1;
    if(fcntl(serv.ctop[0], F_SETFL, flag1 | O_NONBLOCK) == -1)
		return -1;
    if(fcntl(serv.ptoc[0], F_SETFL, flag2 | O_NONBLOCK) == -1)
		return -1;
    
    
    /* Fork the server shell */
    serv.pid = 0;
    
    // create a server's shell.
    if((serv.pid = fork()) == -1)										
		perror("failed to fork");							
    
    // allocate buffers to pass the file descriptors.
    char *buf = (char *)malloc(MSG_SIZE * sizeof(char));
    char *ptr = (char *)malloc(MSG_SIZE * sizeof(char));
    char *ptr1 = (char *)malloc(MSG_SIZE * sizeof(char));
    
    // combine & convert format.
    sprintf(buf, "%s/%s", CURR_DIR, SHELL_PROG);
    sprintf(ptr, "%d", serv.ptoc[0]);
    sprintf(ptr1, "%d", serv.ctop[1]);
    
    // child's code.
    if(serv.pid == 0)
    {
        execl(buf, SHELL_PROG, ptr, ptr1, "server",NULL);
        perror("server's shell failed to execl");
    }
    
    /*
     * Inside the child.
     * Start server's shell.
     * exec the SHELL program with the required program arguments.
     */
    
    /* Inside the parent. This will be the most important part of this program. */
    
    /* Start a loop which runs every 1000 usecs.
     * The loop should read messages from the server shell, parse them using the
     * parse_command() function and take the appropriate actions. */
    
    // allocate buffers to server's shell message.
    char *server_shell_msg = (char *)malloc(MSG_SIZE * sizeof(char));
    
    // the copy of the server's shell message.
    char *buf2 = (char *)malloc(MSG_SIZE * sizeof(char));
    
    while (1) {
        /* Let the CPU breathe */
        
        usleep(1000);
        server_cmd_type cmd;											// define a command object.
        
        /*
         * 1. Read the message from server's shell, if any
         * 2. Parse the command
         * 3. Begin switch statement to identify command and take appropriate action
         *
         * 		List of commands to handle here:
         * 			CHILD_PID
         * 			LIST_USERS
         * 			ADD_USER
         * 			KICK
         * 			EXIT
         * 			BROADCAST
         */
         
        // read from server's shell.
		if(read(serv.ctop[0], server_shell_msg, MSG_SIZE) >= 0){
			cmd = parse_command(server_shell_msg);
            int added_user_idx, idx_to_kick;
            strcpy(buf2, server_shell_msg);
            
            char *token, *kicktok;
            
            // parse the command for different use.
            switch(cmd) {
                case CHILD_PID:
                    token = strtok(buf2, " ");
                    token = strtok(NULL, " ");
                    serv.child_pid = atoi(token);
                    break;
                case LIST_USERS:
                    list_users(users, serv.ptoc[1]);
                    break;
                case ADD_USER:
                    added_user_idx = add_user(users, server_shell_msg, serv.ptoc[1]);
                    break;
                case KICK:
                    kicktok = strtok(buf2, SH_DELIMS);
                    kicktok = strtok(NULL, SH_DELIMS);
                    if(find_user_index(users, kicktok) == -1)
						printf("Invalid user's name!\n");
					else
						cleanup_user(find_user_index(users, kicktok), users);
                    break;
                case EXIT:
                    cleanup_users(users);
                    cleanup_server(serv);
                    break;
                case BROADCAST:
                    broadcast_msg(users, buf2, serv.ptoc[1], "server");
                    break;
                default:
                    break;
			}  
			if(cmd == EXIT)                                             // while loop ends when server shell sees the \exit command
				break;

			if((cmd == ADD_USER) && (added_user_idx != -1)){
				int user_idx = added_user_idx;
				users[added_user_idx].pid = 0;
				users[added_user_idx].pid = fork();                     //fork a new user's xterm window
        
        /* Fork a process if a user was added (ADD_USER) */
        /* Inside the child */
        /*
         * Start an xterm with shell program running inside it.
         * execl(XTERM_PATH, XTERM, "+hold", "-e", <path for the SHELL program>, ..<rest of the arguments for the shell program>..);
         */
				
				// check invalid user index.
				if(users[user_idx].pid == -1) {
					perror("Failed to fork user");
					return -1;
				}
				
				// create an xterm window.
				if(users[user_idx].pid == 0)
				{
					// pass the file descriptors to the pipes.
					char *ptr = (char *)malloc(MSG_SIZE * sizeof(char));
					char *ptr1 = (char *)malloc(MSG_SIZE * sizeof(char));
					sprintf(ptr, "%d", users[user_idx].ptoc[0]);
					sprintf(ptr1, "%d", users[user_idx].ctop[1]);
					
					// start the xterm window.
					execl(XTERM_PATH, XTERM, "+hold", "-e", buf, ptr, ptr1, users[user_idx].name, NULL);
					perror("user's shell xterm window failed to exec");
				}
			}
		}
        
          /* Back to our main while loop for the "parent" */
        /* 
         * Now read messages from the user shells (ie. LOOP) if any, then:
         * 		1. Parse the command
         * 		2. Begin switch statement to identify command and take appropriate action
         *
         * 		List of commands to handle here:
         * 			CHILD_PID
         * 			LIST_USERS
         * 			P2P
         * 			EXIT
         * 			BROADCAST
         *
         * 		3. You may use the failure of pipe read command to check if the 
         * 		user chat windows has been closed. (Remember waitpid with WNOHANG 
         * 		from recitations?)
         * 		Cleanup user if the window is indeed closed.
         */
         
        char *user_shell_msg = (char *)malloc(MSG_SIZE * sizeof(char));
        
        char *tokenpid;
        
        // parse command for valid users.
        for(idx=0; idx<MAX_USERS; idx++){
			if(users[idx].status == SLOT_FULL){
                if(read(users[idx].ctop[0], user_shell_msg, MSG_SIZE) > 0){
					cmd = parse_command(user_shell_msg);
                    
                    switch(cmd) {
                        case CHILD_PID: 								// parse the CHILD_PID of user's command.
                            tokenpid = strtok(user_shell_msg, " ");    
                            tokenpid = strtok(NULL, " ");
                            users[idx].child_pid = atoi(tokenpid);
                            break;
                        case LIST_USERS:
                            list_users(users, users[idx].ptoc[1]);
                            break;
                        case P2P:
                            send_p2p_msg(idx, users, user_shell_msg);
                            break; 
                        case EXIT:
                            cleanup_user(idx, users);
                            break;
                        case BROADCAST:
                            broadcast_msg(users, user_shell_msg, serv.ptoc[1], users[idx].name); 
                            break;
                        default:
                            break;	
                    }
                    
                }
                
                else{
					// test if the zombie OR orphaned processes remain in the system.
					if (waitpid(users[idx].pid, NULL, WNOHANG) < 0){
						cleanup_user(idx,users);
					}
				}
            }
        }   
    }                                
    return 0;
}
