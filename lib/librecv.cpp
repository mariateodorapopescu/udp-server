#include <pthread.h>
#include <cstdlib>
#include <map>
#include <cstdint>
#include "lib.h"
#include "utils.h"
#include "protocol.h"
#include <cassert>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <dispatch/dispatch.h>
#include <random>

#define RECEIVER_PORT 8031

using namespace std;

std::map<int, struct connection *> cons;

struct pollfd data_fds[MAX_CONNECTIONS];
/* Used for timers per connection */
struct pollfd timer_fds[MAX_CONNECTIONS];
int fdmax = 0;

int recv_data(int conn_id, char *buffer, int len) {
 	pthread_mutex_lock(&cons[conn_id]->con_lock);
 	struct connection *conn = cons[conn_id];
 	if (len > MAX_DATA_SIZE - 7) {
 		pthread_mutex_unlock(&cons[conn_id]->con_lock);
 		perror("unlock err");
 		return -1;
 	}
 	while (1) {
 		struct sockaddr_in client_addr;
 		socklen_t clilen = sizeof(client_addr);
 		char buf[MAX_SEGMENT_SIZE];
 		int size = recvfrom(conn->sockfd, buf, MAX_SEGMENT_SIZE, 0, (struct sockaddr*)&client_addr, &clilen);
 
 	// Check if the header is data or control type
 	uint8_t protocol_id = buf[0];
 	uint8_t con_id = buf[1];
 	uint8_t type = buf[2];
 	if (protocol_id != POLI_PROTOCOL_ID || con_id != conn->conn_id) {
 		continue;
 		perror("no match");
 		// The header does not correspond to the protocol or connection
 	}
 	if (type == 1) {
 		// The header is of control type
 		uint16_t ack_num = ntohs(*((uint16_t *)(buf + 3)));
 		uint16_t recv_window = ntohs(*((uint16_t *)(buf + 5)));
 		if (ack_num >= conn->sent_seq_num) {
 			conn->recv_ack_num = ack_num;
 			conn->recv_window = recv_window;
 			// Send ACK
 			uint8_t buf_ack[MAX_SEGMENT_SIZE];
 			memset(&buf_ack, 0, MAX_SEGMENT_SIZE);
 			uint8_t idk = POLI_PROTOCOL_ID;
 			memcpy(buf_ack, &idk, sizeof(uint8_t));
 			memcpy(buf_ack + 1, &conn->conn_id, sizeof(conn->conn_id));
 			uint8_t typee = 1;
 			memcpy(buf_ack + 2, &typee, sizeof(uint8_t));
 			uint16_t recvnum = htons(conn->recv_ack_num);
 			memcpy(buf_ack + 3, &recvnum, sizeof(recvnum));
 			uint16_t recvwin = htons(conn->recv_window);
 			memcpy(buf_ack + 5, &recvwin, sizeof(recvwin));
 			int size_ack = sendto(conn->sockfd, buf_ack, sizeof(buf_ack), 0, (struct sockaddr *)&client_addr, clilen);
 			// Check if all data was received
 			if (ack_num == conn->recv_seq_num) {
 				pthread_mutex_unlock(&cons[conn_id]->con_lock);
 				return 0;
 			}
 		}
 	} else {
 		// The header is of data type
 		uint16_t seq_num = ntohs(*((uint16_t *)(buf + 3)));
 		uint16_t length = ntohs(*((uint16_t *)(buf + 5)));
 		if (length > MAX_DATA_SIZE || seq_num < conn->recv_seq_num || seq_num >= conn->recv_seq_num + conn->recv_window) {
 			// Discard the packet
 			continue;
 		}
 	// Send ACK
 	uint8_t buf_ack[MAX_SEGMENT_SIZE];
 	memset(&buf_ack, 0, MAX_SEGMENT_SIZE);
 	uint8_t idk = POLI_PROTOCOL_ID;
 	memcpy(buf_ack, &idk, sizeof(uint8_t));
 	memcpy(buf_ack + 1, &conn->conn_id, sizeof(conn->conn_id));
 	uint8_t typee = 1;
 	memcpy(buf_ack + 2, &typee, sizeof(uint8_t));
 	uint16_t recvnum = htons(conn->recv_seq_num);
 	memcpy(buf_ack + 3, &recvnum, sizeof(recvnum));
 	uint16_t recvwin = htons(conn->recv_window);
 	memcpy(buf_ack + 5, &recvwin, sizeof(recvwin));
 	int size_ack = sendto(conn->sockfd, buf_ack, sizeof(buf_ack), 0, (struct sockaddr *)&client_addr, clilen);
 	if (size_ack <= 0) {
 		pthread_mutex_unlock(&cons[conn_id]->con_lock);
 		perror("unlock err");
 		return -1;
 	}
 	// Store the data
 	memcpy(buffer, buf + 7, length);
 	conn->recv_seq_num = seq_num + 1;
 	pthread_mutex_unlock(&cons[conn_id]->con_lock);
 	return length;
 	}
 	}
}

void *receiver_handler(void *arg) {

 	char segment[MAX_SEGMENT_SIZE];
 	int res;
 	DEBUG_PRINT("Starting recviver handler\n");

 	while (1) {

 		int conn_id = -1;
 		do {
 			res = recv_message_or_timeout(segment, MAX_SEGMENT_SIZE, &conn_id);
 		} while(res == -14);

 		pthread_mutex_lock(&cons[conn_id]->con_lock);

 		/* Handle segment received from the sender. We use this between locks
 		as to not have synchronization issues with the recv_data calls which are
 		on the main thread */

 		pthread_mutex_unlock(&cons[conn_id]->con_lock);
 	}

}

int wait4connect(uint32_t ip, uint16_t port) {
 	// Allocate memory for the connection structure and initialize the connection ID.
 	struct connection *con = (struct connection *)malloc(sizeof(struct connection));
 	if (!con) {
 		perror("no connection");
 		return -1;
 	}
 	int conn_id = 0;

 	// Set up a socket for receiving 1 packets.
 	con->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
 	if (con->sockfd < 0) {
 		perror("sock err");
 		free(con);
 		return -1;
 	}

 	// Bind the socket to the chosen port.
 	struct sockaddr_in servaddr = {0};
 	servaddr.sin_family = AF_INET;
 	servaddr.sin_addr.s_addr = htonl(ip);
 	servaddr.sin_port = htons(port);
 	if (bind(con->sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
 		close(con->sockfd);
 		perror("bind err");
 		free(con);
 		return -1;
 	}

 	// Set a timeout on the socket.
 	struct timeval timeout = {2, 100000};
 	if (setsockopt(con->sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0) {
 		perror("Error setting socket option");
 	}

 	// Initialize the connection structure and add it to the connections map.
 	pthread_mutex_init(&con->con_lock, NULL);
 	cons.insert({conn_id, con});

 	// Wait for a SYN packet from the client.
 	char buf[MAX_SEGMENT_SIZE] = {0};
 	struct sockaddr_in client_addr = {0};
 	socklen_t addrlen = sizeof(client_addr);
 	int n = recvfrom(con->sockfd, buf, MAX_SEGMENT_SIZE, 0, (struct sockaddr *)&client_addr, &addrlen);
 	if (n < 0) {
 		close(con->sockfd);
 		perror("recvfrom err");
 		free(con);
 		return -1;
 	}

 	// Check if the received packet is a 1 packet.
 	/* Wait for a SYN packet from the client. */
 	addrlen = sizeof(client_addr);
 	memset(buf, 0, MAX_SEGMENT_SIZE);
 	n = recvfrom(con->sockfd, buf, MAX_SEGMENT_SIZE, 0,
 	(struct sockaddr *)&client_addr, &addrlen);
 	if (n < 0) {
 		perror("recvfrom err");
 		return -1;
 	}

 	/* Check if the received packet is a SYN packet. */
 	if (buf[2] == 1) {
 		/* Send a SYN-ACK packet to the client with the chosen data port. */
 
 		// but first let's create a new random port to send to

 		int data_port;
 		srand(time(NULL));
 		// Create a UDP socket
 		int testsockfd = socket(AF_INET, SOCK_DGRAM, 0);
 		if (testsockfd < 0) {
 			perror("finding port err");
 			return -1;
 		}
 	// Bind the socket to a random port
 	struct sockaddr_in testaddr;
 	memset(&testaddr, 0, sizeof(testaddr));
	testaddr.sin_family = AF_INET;
	testaddr.sin_addr.s_addr = htonl(INADDR_ANY);
 	while (true) {
 		data_port = rand() % (65535 - 1024) + 1024; // Generate a random port in the range [1024, 65535]
 		testaddr.sin_port = htons(data_port);
 		int ret = bind(testsockfd, (struct sockaddr*)&testaddr, sizeof(testaddr));
 		if (ret == 0) {
 			perror("finding port err");
 			break;
 		}
 	}
 	// Close the socket
 	close(testsockfd);

 	// if it got there, then it was found an aviable data_port
 	struct poli_tcp_ctrl_hdr syn_ack_hdr;
 	memset(&syn_ack_hdr, 0, sizeof(syn_ack_hdr));
 	syn_ack_hdr.protocol_id = POLI_PROTOCOL_ID;
 	syn_ack_hdr.conn_id = conn_id;
 	syn_ack_hdr.ack_num = 0;
 	syn_ack_hdr.recv_window = sizeof(syn_ack_hdr);
 	syn_ack_hdr.type = 1;
 	syn_ack_hdr.port = data_port;

 	struct poli_tcp_ctrl_pkt pachet;
 	memset(&pachet, 0, sizeof(pachet));
 	pachet.header = syn_ack_hdr;
 	memcpy(pachet.payload, &data_port, sizeof(data_port));

 	// Set up a socket for receiving data packets.
 	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
 	if (sockfd < 0) {
 		close(con->sockfd);
 		perror("sock err");
 		free(con);
 		return -1;
 	}

 	// Bind the socket to the chosen port.
 	if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
 		close(con->sockfd);
 		close(sockfd);
 		perror("bind err");
 		free(con);
 		return -1;
 	}

	// Update the connection structure with the data socket and client address.
 	con->sockfd = sockfd;
 	con->client_addr = client_addr;

 	// Create a new thread to handle incoming data packets for this connection.
 	pthread_t thread;
 	if (pthread_create(&thread, NULL, receiver_handler, (void *)&conn_id) != 0) {
 		close(con->sockfd);
 		close(sockfd);
 		perror("pthreadcreate err");
 		free(con);
 		return -1;
 	}
 	}
 	return conn_id;
}


void init_receiver(int recv_buffer_bytes) {
 	pthread_t thread1;
 	int ret;

 	int sockfd;
 	struct sockaddr_in receiver_addr;

 	// Create the connection socket
 	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
 	if (sockfd < 0) {
 		perror("socket");
 		exit(1);
 	}

 	// Bind the socket to RECEIVER_PORT
 	memset(&receiver_addr, 0, sizeof(receiver_addr));
 	receiver_addr.sin_family = AF_INET;
 	receiver_addr.sin_addr.s_addr = INADDR_ANY;
 	receiver_addr.sin_port = htons(RECEIVER_PORT);
 	if (bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
 		perror("bind");
 		exit(1);
 	}

 	ret = pthread_create(&thread1, NULL, receiver_handler, (void*)&sockfd);
 	assert(ret == 0);
}
