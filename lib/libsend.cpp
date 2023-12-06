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
#include <errno.h>


using namespace std;

std::map<int, struct connection *> cons;

struct pollfd data_fds[MAX_CONNECTIONS];
/* Used for timers per connection */
struct pollfd timer_fds[MAX_CONNECTIONS];
int fdmax = 0;

int send_data(int conn_id, char *buffer, int len) {
 	int size = 0;
 	pthread_mutex_lock(&cons[conn_id]->con_lock);
 	int n;
 	struct connection* con = cons[conn_id];

 	// Keep sending data until all bytes have been sent or there is an error
 	while (size < len) {
 		n = send(con->sockfd, buffer + size, len - size, 0);
 		if (n == -1) {
 			if (errno == EAGAIN || errno == EWOULDBLOCK) {
 				// The send buffer is full, wait a bit and try again
 				struct timespec ts = {0, 100000}; // 100 us
 				nanosleep(&ts, NULL);
 				perror("EAGAIN");
 				continue;
 			} else {
 				// There was an error sending data
 				pthread_mutex_unlock(&con->con_lock);
 				return -1;
 			}
 		} else if (n == 0) {
 			// The connection was closed
 			pthread_mutex_unlock(&con->con_lock);
 			perror("closed connection");
 			return -1;
 		} else {
 			size += n;
 		}
 	}

 	pthread_mutex_unlock(&cons[conn_id]->con_lock);

 	return size;
}

void *sender_handler(void *arg) {
 	int res = 0;
 	char buf[MAX_SEGMENT_SIZE];

 	while (1) {

		// If there are no connections, skip this iteration of the loop
		if (cons.size() == 0) {
			perror("no connections existing");
			continue;
		}

		int conn_id = -1;
		do {
			res = recv_message_or_timeout(buf, MAX_SEGMENT_SIZE, &conn_id);
		} while(res == -14);

		pthread_mutex_lock(&cons[conn_id]->con_lock);

		pthread_mutex_unlock(&cons[conn_id]->con_lock);
	}
}

int setup_connection(uint32_t ip, uint16_t port) {
 	/* Implement the sender part of the Three Way Handshake. Blocks
 	until the connection is established */
 	struct connection *con = (struct connection *)malloc(sizeof(struct connection));

 	if (con == NULL) {
 		perror("malloc failed");
 		return -1;
 	}

 	int conn_id = 0;
 	con->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

 	// const char* ipString = "127.0.0.1";
 	// uint32_t ipAddress;

 	// inet_pton(AF_INET, ipString, &ipAddress);

 	// This can be used to set a timer on a socket
 	struct timeval tv;
 	tv.tv_sec = 2;
 	tv.tv_usec = 100000;
 	if (setsockopt(con->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
 		perror("setsockopt err");
 	}

 	int reuse = 1;
 	if (setsockopt(con->sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
 		perror("setsockopt SO_REUSEADDR");
 		return -1;
 	}

 	/* Set up the server address */
 	memset(&con->servaddr, 0, sizeof(con->servaddr));
 	con->servaddr.sin_family = AF_INET;
 	// con->servaddr.sin_addr.s_addr = ipAddress;
 	con->servaddr.sin_addr.s_addr = ip;
 	con->servaddr.sin_port = port;
 	socklen_t siz = sizeof(con->servaddr);
 	if (bind(con->sockfd, (struct sockaddr *)&con->servaddr, siz) < 0) {
 		perror("bind");
 		return -1;
 	}

 	/* We will send the SYN on 8031. Then we will receive a SYN-ACK with the connection
	 * port. We can use con->sockfd for both cases, but we will need to update server_addr
	 * with the port received via SYN-ACK */

 	/* Since we can have multiple connection, we want to know if data is available
 	on the socket used by a given connection. We use POLL for this */
 	data_fds[fdmax].fd = con->sockfd; 
 	data_fds[fdmax].events = POLLIN; 
 
 	/* This creates a timer and sets it to trigger every 1 sec. We use this
 	to know if a timeout has happend on our connection */
 	timer_fds[fdmax].fd = timerfd_create(CLOCK_REALTIME, 0); 
 	timer_fds[fdmax].events = POLLIN; 
 	struct itimerspec spec; 
 	spec.it_value.tv_sec = 1; 
 	spec.it_value.tv_nsec = 0; 
 	spec.it_interval.tv_sec = 1; 
 	spec.it_interval.tv_nsec = 0; 
 	timerfd_settime(timer_fds[fdmax].fd, 0, &spec, NULL); 
 	fdmax++;

 	pthread_mutex_init(&con->con_lock, NULL);
 	// cons.insert({conn_id, con});
 	cons[conn_id] = con;

 	/* Create SYN segment */
 	struct poli_tcp_ctrl_hdr syn_hdr;
 	memset(&syn_hdr, 0, sizeof(syn_hdr));
 	syn_hdr.protocol_id = POLI_PROTOCOL_ID;
 	syn_hdr.conn_id = conn_id;
 	syn_hdr.ack_num = 0;
 	syn_hdr.recv_window = sizeof(syn_hdr);
 	syn_hdr.type = 1;
 	struct poli_tcp_ctrl_pkt idk;
 	memset(&idk, 0, sizeof(idk));
 	idk.header = syn_hdr;

 	/* Send SYN segment */
 	socklen_t thesize = sizeof(cons[conn_id]->servaddr);
 	int n = sendto(cons[conn_id]->sockfd, &idk, sizeof(idk), 0, (const struct sockaddr *)&cons[conn_id]->servaddr, thesize);
 	if (n < 0) {
 		perror("send err");
 		exit(EXIT_FAILURE);
 	}

 	/* Wait for a SYN-ACK packet from the server. */
 	char buf[MAX_SEGMENT_SIZE];
 	memset(buf, 0, MAX_SEGMENT_SIZE);
 	socklen_t nuj = sizeof(cons[conn_id]->servaddr);
 	n = recvfrom(con->sockfd, &buf,
 			MAX_SEGMENT_SIZE,
 			0,
 			(struct sockaddr *)&cons[conn_id]->servaddr,
 			&nuj);
 	if (n < 0) {
 		if (errno != EAGAIN || errno != EWOULDBLOCK) {
 			perror("recvfrom err");
 			return -1;
 		}
 	}

 	/* Parse the SYN-ACK packet and update the connection structure. */
 	struct poli_tcp_ctrl_pkt *syn_ack_pkt = (struct poli_tcp_ctrl_pkt *)&buf;
 	struct poli_tcp_ctrl_hdr syn_ack_hdr = syn_ack_pkt->header;
 	if (syn_ack_hdr.protocol_id != POLI_PROTOCOL_ID || syn_ack_hdr.type != 1) {
 		perror("matching errr");
 		return -1;
 	}

 	con->servaddr = cons[conn_id]->servaddr;
 	con->conn_id = conn_id;
 	con->recv_window = syn_ack_hdr.recv_window;
 	con->recv_window = MAX_SEGMENT_SIZE;

 	/* Update the server address with the port received via SYN-ACK */

 	cons[conn_id]->servaddr.sin_port = syn_ack_pkt->header.port;

 	/* Send an ACK packet to the server. */
 	struct poli_tcp_ctrl_hdr ack_hdr;
 	memset(&ack_hdr, 0, sizeof(ack_hdr));
 	ack_hdr.protocol_id = POLI_PROTOCOL_ID;
 	ack_hdr.conn_id = conn_id;
 	ack_hdr.ack_num = syn_ack_hdr.ack_num + 1;
 	ack_hdr.recv_window = MAX_SEGMENT_SIZE;
 	ack_hdr.type = 2;

 	struct poli_tcp_ctrl_pkt ack_pkt;
 	memset(&ack_pkt, 0, sizeof(ack_pkt));
 	ack_pkt.header = ack_hdr;

 	socklen_t nush = sizeof(cons[conn_id]->servaddr); // Make sure to initialize nush with the correct value

	n = sendto(con->sockfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&cons[conn_id]->servaddr, nush);
	if (n < 0) {
 		if (errno == EBADF) {
 		std::cerr << "sendto error: Bad file descriptor" << std::endl;
 	} else if (errno == ENOTSOCK) {
 		std::cerr << "sendto error: Not a socket" << std::endl;
 	} else if (errno == EFAULT) {
 		std::cerr << "sendto error: Invalid memory address" << std::endl;
 	} else if (errno == EMSGSIZE) {
 		std::cerr << "sendto error: Message too long" << std::endl;
 	} else {
 		std::cerr << "sendto error: " << strerror(errno) << std::endl;
 	}
 		return -1;
	}

	/* Start the connection timeout timer. */
 	pthread_t timeout_thread;
 	pthread_create(&timeout_thread, NULL, sender_handler, &con);

 	DEBUG_PRINT("Connection established!");
 	return 0;
}

void init_sender(int speed, int delay) {
 	pthread_t thread1;
 	int ret;

 	ret = pthread_create(&thread1, NULL, sender_handler, NULL);
 	assert(ret == 0);
}
