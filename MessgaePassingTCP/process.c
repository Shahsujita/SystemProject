//Assignment 3
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "process.h"

// messaging config
int WINDOW_SIZE;
int MAX_DELAY;
int TIMEOUT;
int DROP_RATE;

// process information
process_t myinfo;
int mailbox_id;
// a message id is used by the receiver to distinguish a message from other messages
// you can simply increment the message id once the message is completed
int message_id = 0;
// a mark that indicates that we can malloc the message
int flag_malloc = 1;
// the message status is used by the sender to monitor the status of a message
message_status_t message_stats;
// the message is used by the receiver to store the actual content of a message
message_t *message;

int num_available_packets; // number of packets that can be sent (0 <= n <= WINDOW_SIZE)

int is_receiving = 0; // a helper varibale may be used to handle multiple senders

int ack_notify = 0;

int ct_pkt_rec = 0;

int ct_tmout = 0;

int call_get_proc_info = 0;

int max_tmout_flag = 0;


/**
 * TODO complete the definition of the function
 * 1. Save the process information to a file and a process structure for future use.
 * 2. Setup a message queue with a given key.
 * 3. Setup the signal handlers (SIGIO for handling packet, SIGALRM for timeout).
 * Return 0 if success, -1 otherwise.
 */
int init(char *process_name, key_t key, int wsize, int delay, int to, int drop) {
    myinfo.pid = getpid();
    strcpy(myinfo.process_name, process_name);
    myinfo.key = key;

    // open the file
    FILE* fp = fopen(myinfo.process_name, "wb");
    if (fp == NULL) {
        printf("Failed opening file: %s\n", myinfo.process_name);
        return -1;
    }
    // write the process_name and its message keys to the file
    if (fprintf(fp, "pid:%d\nprocess_name:%s\nkey:%d\n", myinfo.pid, myinfo.process_name, myinfo.key) < 0) {
        printf("Failed writing to the file\n");
        return -1;
    }
    fclose(fp);

    WINDOW_SIZE = wsize;
    MAX_DELAY = delay;
    TIMEOUT = to;
    DROP_RATE = drop;

    printf("[%s] pid: %d, key: %d\n", myinfo.process_name, myinfo.pid, myinfo.key);
    printf("window_size: %d, max delay: %d, timeout: %d, drop rate: %d%%\n", WINDOW_SIZE, MAX_DELAY, TIMEOUT, DROP_RATE);

    // TODO setup a message queue and save the id to the mailbox_id
    mailbox_id = msgget(myinfo.key, 0666 | IPC_CREAT);
    if (mailbox_id == -1) {
		printf("Failed to set up message queue in init function.\n");
		return -1;
	}
	
	signal(SIGIO, receive_packet);
	signal(SIGALRM, timeout_handler);

    return 0;
}

/**
 * Get a process' information and save it to the process_t struct.
 * Return 0 if success, -1 otherwise.
 */
int get_process_info(char *process_name, process_t *info) {
    char buffer[MAX_SIZE];
    char *token;

    // open the file for reading
    FILE* fp = fopen(process_name, "r");
    if (fp == NULL) {
        return -1;
    }
    // parse the information and save it to a process_info struct
    while (fgets(buffer, MAX_SIZE, fp) != NULL) {
        token = strtok(buffer, ":");
        if (strcmp(token, "pid") == 0) {
            token = strtok(NULL, ":");
            info->pid = atoi(token);
        } else if (strcmp(token, "process_name") == 0) {
            token = strtok(NULL, ":");
            strcpy(info->process_name, token);
        } else if (strcmp(token, "key") == 0) {
            token = strtok(NULL, ":");
            info->key = atoi(token);
        }
    }
    fclose(fp);
    return 0;
}

/**
 * TODO Send a packet to a mailbox identified by the mailbox_id, and send a SIGIO to the pid.
 * Return 0 if success, -1 otherwise.
 */
int send_packet(packet_t *packet, int mailbox_id, int pid) {
	int error_snd_pkt = 0;
	if (msgsnd(mailbox_id, (void *)packet, sizeof (packet_t), 0) == -1) {
		error_snd_pkt = errno;
		return -1;
	}
	if (kill(pid, SIGIO) == -1)
		perror("Failed to send SIGIO signal");
	
	printf("Send a packet [%d] to pid:%u\n", packet->packet_num, pid);
    return 0;
}

/**
 * Get the number of packets needed to send a data, given a packet size.
 * Return the number of packets if success, -1 otherwise.
 */
int get_num_packets(char *data, int packet_size) {
    if (data == NULL) {
        return -1;
    }
    if (strlen(data) % packet_size == 0) {
        return strlen(data) / packet_size;
    } else {
        return (strlen(data) / packet_size) + 1;
    }
}

/**
 * Create packets for the corresponding data and save it to the message_stats.
 * Return 0 if success, -1 otherwise.
 */
int create_packets(char *data, message_status_t *message_stats) {
    if (data == NULL || message_stats == NULL) {
        return -1;
    }
    int i, len;
    for (i = 0; i < message_stats->num_packets; i++) {
        if (i == message_stats->num_packets - 1) {
            len = strlen(data)-(i*PACKET_SIZE);
        } else {
            len = PACKET_SIZE;
        }
        message_stats->packet_status[i].is_sent = 0;
        message_stats->packet_status[i].ACK_received = 0;
        message_stats->packet_status[i].packet.message_id = -1;
        message_stats->packet_status[i].packet.mtype = DATA;
        message_stats->packet_status[i].packet.pid = myinfo.pid;
        strcpy(message_stats->packet_status[i].packet.process_name, myinfo.process_name);
        message_stats->packet_status[i].packet.num_packets = message_stats->num_packets;
        message_stats->packet_status[i].packet.packet_num = i;
        message_stats->packet_status[i].packet.total_size = strlen(data);
        memcpy(message_stats->packet_status[i].packet.data, data+(i*PACKET_SIZE), len);
        message_stats->packet_status[i].packet.data[len] = '\0';
    }
    return 0;
}

/**
 * Get the index of the next packet to be sent.
 * Return the index of the packet if success, -1 otherwise.
 */
int get_next_packet(int num_packets) {
    int packet_idx = rand() % num_packets;
    int i = 0;

    i = 0;
    while (i < num_packets) {
        if (message_stats.packet_status[packet_idx].is_sent == 0) {
            // found a packet that has not been sent
            return packet_idx;
        } else if (packet_idx == num_packets-1) {
            packet_idx = 0;
        } else {
            packet_idx++;
        }
        i++;
    }
    // all packets have been sent
    return -1;
}

/**
 * Use probability to simulate packet loss.
 * Return 1 if the packet should be dropped, 0 otherwise.
 */
int drop_packet() {
    if (rand() % 100 > DROP_RATE) {
        return 0;
    }
    return 1;
}

/**
 * TODO Send a message (broken down into multiple packets) to another process.
 * We first need to get the receiver's information and construct the status of
 * each of the packet.
 * Return 0 if success, -1 otherwise.
 */
int send_message(char *receiver, char* content) {
    if (receiver == NULL || content == NULL) {
        printf("Receiver or content is NULL\n");
        return -1;
    }
    // get the receiver's information
    if (get_process_info(receiver, &message_stats.receiver_info) < 0) {
        printf("Failed getting %s's information.\n", receiver);
        return -1;
    }
    // get the receiver's mailbox id
    message_stats.mailbox_id = msgget(message_stats.receiver_info.key, 0666);
    if (message_stats.mailbox_id == -1) {
        printf("Failed getting the receiver's mailbox.\n");
        return -1;
    }
    // get the number of packets
    int num_packets = get_num_packets(content, PACKET_SIZE);
    if (num_packets < 0) {
        printf("Failed getting the number of packets.\n");
        return -1;
    }
    // set the number of available packets
    if (num_packets > WINDOW_SIZE) {
        num_available_packets = WINDOW_SIZE;
    } else {
        num_available_packets = num_packets;
    }

    // setup the information of the message
    message_stats.is_sending = 1;
    message_stats.num_packets_received = 0;
    message_stats.num_packets = num_packets;
    message_stats.packet_status = malloc(num_packets * sizeof(packet_status_t));
    if (message_stats.packet_status == NULL) {
        return -1;
    }
    
    // parition the message into packets
    if (create_packets(content, &message_stats) < 0) {
        printf("Failed paritioning data into packets.\n");
        message_stats.is_sending = 0;
        free(message_stats.packet_status);
        return -1;
    }

    // TODO send packets to the receiver
    // the number of packets sent at a time depends on the WINDOW_SIZE.
    // you need to change the message_id of each packet (initialized to -1)
    // with the message_id included in the ACK packet sent by the receiver
	
	int packet_idx;
	if ((packet_idx = get_next_packet(num_packets)) == -1) {
		return -1;
	}
	//First, send a single packet, update its "is_sent" status
	send_packet(&(message_stats.packet_status[packet_idx].packet),message_stats.mailbox_id, message_stats.receiver_info.pid);
	message_stats.packet_status[packet_idx].is_sent = 1;

	alarm(TIMEOUT);
	while(!ack_notify && max_tmout_flag == 0){
		pause();//Wait for SIGIO or SIGALRM
		//If after SIGIO i.e. receive_packet(), we can make sure we have received the ACK of the first packet
		if (message_stats.packet_status[packet_idx].ACK_received == 1) {
			ack_notify = 1;
			num_available_packets--;
		}
	}
	
	
	//Sliding Window
	while (message_stats.num_packets_received != message_stats.num_packets && max_tmout_flag == 0) {
		sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	//sigaddset(&set, SIGIO);
	sigprocmask(SIG_SETMASK, &set, NULL);
		while (num_available_packets > 0) {
			printf("num_available_packets = %d\n", num_available_packets);
			if ((packet_idx = get_next_packet(num_packets)) == -1) {
				printf("Invalid index!\n");
				break;
			}
			printf("1\n");
			//Update the message_id obtained by the ACK of the first packet
			message_stats.packet_status[packet_idx].packet.message_id = message_id;
			printf("2\n");
			//send this randomly chosen packet
			if (send_packet(&(message_stats.packet_status[packet_idx].packet),message_stats.mailbox_id, message_stats.receiver_info.pid) == -1)
				return -1;
			printf("3\n");
			//Update its "is_sent" status
			message_stats.packet_status[packet_idx].is_sent = 1;
			printf("4\n");
			num_available_packets--;
			printf("5\n");
		}
		sigprocmask(SIG_UNBLOCK, &set, NULL);
		alarm(TIMEOUT);
		//pause();
	}
	if (max_tmout_flag == 0) {
		printf("All packets sent.\n");
	}
	alarm(0);
	message_stats.num_packets_received = 0;
	message_stats.num_packets = 0;
	message_stats.is_sending = 0;
	ack_notify = 0;
	message_id = 0;
	max_tmout_flag = 0;
	free(message_stats.packet_status);
    return 0;
}

/**
 * TODO Handle TIMEOUT. Resend previously sent packets whose ACKs have not been
 * received yet. Reset the TIMEOUT.
 */
 
void timeout_handler(int sig) {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigaddset(&set, SIGIO);
	sigprocmask(SIG_SETMASK, &set, NULL);
	
	if(message_stats.is_sending != 1){
		printf("resetting to zero\n");
		alarm(0);
	}
	else {
		printf("TIMEOUT! ");
		int i;
		
		//Resend previously sent packets whose ACKs have not been received yet. 
		if (ct_tmout >= MAX_TIMEOUT) {
			printf("Max Timeout reached!\n");
			ct_tmout = 0;
			max_tmout_flag = 1;
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			printf("Failed to send this message!\n");
			return;
		}
		
		for(i = 0; i < message_stats.num_packets; i++) {
			
			if((message_stats.packet_status[i].is_sent == 1) && (message_stats.packet_status[i].ACK_received == 0)) {
				send_packet(&(message_stats.packet_status[i].packet),message_stats.mailbox_id, message_stats.receiver_info.pid);
				 		
			}
		 }
		 
		 ct_tmout++;
		 alarm(TIMEOUT);
	}
	sigprocmask(SIG_UNBLOCK, &set, NULL);
	
}

/**
 * TODO Send an ACK to the sender's mailbox.
 * The message id is determined by the receiver and has to be included in the ACK packet.
 * Return 0 if success, -1 otherwise.
 */
//Using by the receiver, sender's mailbox_id, sender's pid and the index of the ACK we want to send
int send_ACK(int mailbox_id, pid_t pid, int packet_num) {
    
    // TODO construct an ACK packet
    packet_t ACK_packet;
    ACK_packet.mtype = ACK;
    ACK_packet.message_id = message_id;
    ACK_packet.pid = myinfo.pid;
    strcpy(ACK_packet.process_name, myinfo.process_name);
    ACK_packet.packet_num = packet_num;
    
    if(MAX_DELAY !=0){
		int delay = rand() % MAX_DELAY;
		sleep(delay);
	}
	

    // TODO send an ACK for the packet it received
    int error_snd_pkt = 0;
	if (msgsnd(mailbox_id, (void *)&ACK_packet, sizeof (packet_t), 0) == -1) {
		error_snd_pkt = errno;
		return -1;
	}
	kill(pid, SIGIO);
	printf("Send an ACK for packet %d to pid:%d\n", packet_num, pid);
    return 0;
}

/**
 * TODO Handle DATA packet. Save the packet's data and send an ACK to the sender.
 * You should handle unexpected cases such as duplicate packet, packet for a different message,
 * packet from a different sender, etc.
 */
void handle_data(packet_t *packet, process_t *sender, int sender_mailbox_id) { 
	message->sender.pid = sender->pid;
	strcpy(message->sender.process_name, sender->process_name);
	message->sender.key = sender->key;
	message->num_packets_received++;
	
	if (packet->num_packets == message->num_packets_received) {
		message->is_complete = 1;
		message_id++;
	}
	message->is_received[packet->packet_num] = 1;
	
	memcpy(message->data + (packet->packet_num * PACKET_SIZE),packet->data,sizeof(char)*PACKET_SIZE);
	
	if (packet->packet_num == packet->num_packets - 1) {
		
		message->data[packet->packet_num * PACKET_SIZE + strlen(packet->data)] = '\0';
	}
	
	send_ACK(sender_mailbox_id, sender->pid, packet->packet_num);
}

/**
 * TODO Handle ACK packet. Update the status of the packet to indicate that the packet
 * has been successfully received and reset the TIMEOUT.
 * You should handle unexpected cases such as duplicate ACKs, ACK for completed message, etc.
 */
//sender, the receiver sent the ack to the sender and upadate status and counter
void handle_ACK(packet_t *packet) {  
	
	if(message_stats.is_sending == 1){ 	
		message_stats.packet_status[packet->packet_num].ACK_received = 1;
		printf("Receive an ACK for packet [%d]\n", packet->packet_num);
		
		//Update the first packet's message_id by the received ACK
		message_stats.packet_status[packet->packet_num].packet.message_id = packet->message_id;
		message_id = packet->message_id;
		num_available_packets++;
		message_stats.num_packets_received++;
		alarm(TIMEOUT);
		ct_tmout = 0;
		
   }
}

/**
 * Get the next packet (if any) from a mailbox.
 * Return 0 (false) if there is no packet in the mailbox
 */
int get_packet_from_mailbox(int mailbox_id) {
    struct msqid_ds buf;

    return (msgctl(mailbox_id, IPC_STAT, &buf) == 0) && (buf.msg_qnum > 0);
}

/**
 * TODO Receive a packet.
 * If the packet is DATA, send an ACK packet and SIGIO to the sender.
 * If the packet is ACK, update the status of the packet.
 */
void receive_packet(int sig) {
	fflush(stdout);
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigaddset(&set, SIGIO);
	sigprocmask(SIG_SETMASK, &set, NULL);
	

	fflush(stdout);
	
	packet_t rec_packet;
	
	if (msgrcv(mailbox_id, &rec_packet, sizeof (packet_t), 0, 0) == -1){
		perror("failed to read message queue");
	}

	fflush(stdout);
	if (call_get_proc_info == 0) {
		if (get_process_info(rec_packet.process_name, &message_stats.receiver_info) == -1) {
			printf("failed getting %s's information.\n", rec_packet.process_name);
		}
		call_get_proc_info++;
	}
	message_stats.mailbox_id = msgget(message_stats.receiver_info.key, 0666);
	
	if (drop_packet()) {
		if(message_stats.is_sending == 1){
			printf("Packet dropped!\n");
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
	    }
    }
	
  
	if (rec_packet.mtype == ACK) {
		if (message_stats.packet_status[rec_packet.packet_num].ACK_received == 0) {
			handle_ACK(&rec_packet);
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
		}
		else if (message_stats.packet_status[rec_packet.packet_num].ACK_received == 1){
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
		}
	}
	else if (rec_packet.mtype == DATA) {
		if (is_receiving == 0) {
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
		}
		
		if (rec_packet.pid != message_stats.receiver_info.pid) {
			printf("Ignore the latter senders!\n");
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
		}
		
		if (rec_packet.message_id == -1 && flag_malloc == 1) {
			fflush(stdout);
			message->is_received = (int *) malloc(rec_packet.num_packets * sizeof(int));
			message->data = (char *) malloc(rec_packet.num_packets * PACKET_SIZE * sizeof(char));
			memset(message->is_received, 0, rec_packet.num_packets * sizeof(int));
			memset(message->data, 0, rec_packet.num_packets * PACKET_SIZE * sizeof(char));
			flag_malloc = 0;
			
		}
		else if (message_id != rec_packet.message_id && flag_malloc == 0 ) { 
			send_ACK(message_stats.mailbox_id, message_stats.receiver_info.pid, rec_packet.packet_num);
			sigprocmask(SIG_UNBLOCK, &set, NULL);
			return;
	    }
	    
	    if (message->is_received[rec_packet.packet_num] == 1) {
			if (message_id == rec_packet.message_id) {
				send_ACK(message_stats.mailbox_id, message_stats.receiver_info.pid, rec_packet.packet_num);
				sigprocmask(SIG_UNBLOCK, &set, NULL);
				return;
			}
		}
		
		handle_data(&rec_packet, &(message_stats.receiver_info), message_stats.mailbox_id);
	}
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/**
 * TODO Initialize the message structure and wait for a message from another process.
 * Save the message content to the data and return 0 if success, -1 otherwise
 */
int receive_message(char *data) {
	message = (message_t *) malloc(sizeof (message_t));
	message->num_packets_received = 0;
	message->is_complete = 0;
	int msg_rec = 0;
	flag_malloc = 1;
	
	call_get_proc_info = 0;
	is_receiving = 1;

	while(!msg_rec) {
		pause();
		if (message->is_complete == 1) {
		    printf("All packets received.\n");
			msg_rec = 1;
			call_get_proc_info = 0;

		}
	}
	strcpy(data, message->data);
	free(message->is_received);
	free(message->data);
	free(message);
	flag_malloc = 0;
	ct_tmout = 0;
	is_receiving = 0;

	//Clean up the message queue, especially the redundant duplicate data packet
	while (get_packet_from_mailbox(mailbox_id) == 1) {
		packet_t rec_packet;
		if (msgrcv(mailbox_id, &rec_packet, sizeof (packet_t), 0, 0) == -1) {
			printf("Failed to clean up the message queue!\n");
		}
	}
    return 0;
}
