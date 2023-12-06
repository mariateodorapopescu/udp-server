
#pragma once

#include <cstdint>
#include "utils.h"
#include <arpa/inet.h>

/* Maximum segment size, change as you see fit */
#define MAX_DATA_SIZE 512
#define MAX_SEGMENT_SIZE (MAX_DATA_SIZE + sizeof(poli_tcp_data_hdr))

#define MAX_CONNECTIONS 32

/* Protocol control block. Used track different parameters about a connection. 
 * Will need to be extenden to solve the homework with other parameters such as
 * last_ack or status depending on how you implement your protocol. */
struct connection {
    /* Common window for both the sender and receiver */
    /* List window: A window representation */
    int sockfd; /* Socket used for this connection */
    int conn_id; /* Connection identifier */
    struct sockaddr_in servaddr; /* Used to identify the destination */
    pthread_mutex_t con_lock; /* Used for synchronization with the handler thread and read/send calls */

    /* Parameters used only by the sender */
    int sent_seq_num;
    int sent_ack_num;

    /* Parameters used only by the client */
    int recv_seq_num;
    int recv_ack_num;
    int recv_window;
    int timer_fd;
	char **recv_buf;
	int recv_buf_size;
	FILE* fp;
	struct sockaddr_in client_addr;
};


/* ########## API that we expose to the application ########### */

/* Equivalent of listen. Ran by the server to waits for a connection from a
 * client. Returns a connection id. Blocking untill it receives a connection
 * request */
int wait4connect(uint32_t ip, uint16_t port);
/* Equivalent of connect. Used by the client to connect to a server. */
int setup_connection(uint32_t ip, uint16_t port);
/* Equivalent to recv. Blocking if there is no data to be written in buffer */
int recv_data(int connectionid, char *buffer, int len);
/* Equivalent to send. Used by the client to send a stream of bytes as segments */
int send_data(int conn_id, char *buffer, int len);
/* Used to initialize your protocol on the receiver side. */
void init_receiver(int recv_buffer_bytes);
/* Used to initialize your protocol on the sender side */
void init_sender(int speed, int delay);

/* ######### Internal API used by sender and receiver ########### */
int recv_message_or_timeout(char *buff, size_t len, int *conn_id);
