/*
 * Copyright 2013 Kristian Evensen <kristian.evensen@gmail.com>
 *
 * This file is part of Bandwidth Estimator. Bandwidth Estimator is free
 * software: you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * Bandwidth Estimatior is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Bandwidth Estimator. If not, see http://www.gnu.org/licenses/.
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>

#include "bw_estimation_packets.h"

#define NUM_THREADS 4
#define NUM_END_SESSION_PKTS 10

typedef enum{
    PAUSED,
    RUNNING
} thread_status;

//Make multithreaded. Need a hashmap here
struct thread_info{
    thread_status status;
    int32_t udp_sock_fd;
    struct sockaddr_storage source;
    uint16_t num_packets;
    uint16_t num_bursts;
    uint16_t payload_len;
    //I am a bit lazy, so just store it instead of multiple lookups
    uint16_t remote_port;
    pthread_cond_t new_session;
    pthread_mutex_t new_session_mutex;
};

//Bind the local socket. Should work with both IPv4 and IPv6
int bind_local(char *local_addr, char *local_port, int socktype, uint8_t 
        listen_socket){
    struct addrinfo hints, *info, *p;
    int sockfd;
    int rv;
    int yes=1;
  
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
  
    if((rv = getaddrinfo(local_addr, local_port, &hints, &info)) != 0){
        fprintf(stdout, "Getaddrinfo (local) failed: %s\n", gai_strerror(rv));
        return -1;
    }
  
    for(p = info; p != NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) 
                == -1){
            fprintf(stdout, "Socket:");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) 
                == -1) {
            close(sockfd);
            fprintf(stdout, "Setsockopt (reuseaddr)");
            continue;
        }

        if(setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(int)) 
                == -1){
            close(sockfd);
            fprintf(stdout, "Setsockopt (timestamp)");
            continue;
        }

        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            fprintf(stdout, "Bind (local)");
            continue;
        }

        if(listen_socket && listen(sockfd, NUM_THREADS) == -1){
            close(sockfd);
            fprintf(stdout, "Connect");
            continue;
        }

        break;
    }

    if(p == NULL){
        fprintf(stdout, "Local bind failed\n");
        freeaddrinfo(info);  
        return -1;
    }

    freeaddrinfo(info);

    return sockfd;
}



uint64_t generate_udp_traffic(struct thread_info *thread_info){
    //Variables used to compute and keep the desired bandwidth
    struct timeval t0_p, t1_p;
    time_t t0, t1;
    double pkts_per_sec = 0; 
    double desired_iat = 0;
    double iat = 0;
    double adjust = 0;
    int32_t i;
    uint64_t tot_bytes = 0;

    //Used for sending packet
    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {DATA};

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));
    msg.msg_name = (void *) &(thread_info->source);
    msg.msg_namelen = sizeof(struct sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    iov.iov_base = buf;
    iov.iov_len = thread_info->payload_len;

   /* pkts_per_sec = (thread_info->bandwidth * 1000 * 1000) / 
        (double) (thread_info->payload_len * 8);
    desired_iat = 1000000 / pkts_per_sec; //IAT is microseconds, ok resolution
    fprintf(stdout, "Bandiwdth of %d Mbit/s, duration %ds, payload length %d "
            "byte\n", thread_info->bandwidth, thread_info->duration, 
            thread_info->payload_len);*/
    fprintf(stdout, "Sending %d packets/s\n", 
            thread_info->num_packets);

  
    
    
    //Remember to reset values
    buf[0] = DATA;
    iat = 0;
    tot_bytes = 0;
    int sent_packet=0;
    int sent_burst=0;
    double time_left;
   
    while(sent_burst<thread_info->num_bursts){
        gettimeofday(&t0_p, NULL); 
	sent_packet=0;
    	while(sent_packet<thread_info->num_packets){


        	tot_bytes += sendmsg(thread_info->udp_sock_fd, &msg, 0);
		sent_packet++;
        	
    	}
        gettimeofday(&t1_p, NULL);
        time_left=((t1_p.tv_sec - t0_p.tv_sec) * 1000 + (double)(t1_p.tv_usec - t0_p.tv_usec) / 1000);
	if (time_left<1000){
		usleep((1000-time_left)*1000);
        }
	sent_burst++;
	
    }

    printf("%d-- %d",sent_burst,thread_info->num_bursts);

    //Easy solution for sending end_session
    buf[0] = END_SESSION;
    for(i = 0; i<NUM_END_SESSION_PKTS; i++)
        sendmsg(thread_info->udp_sock_fd, &msg, 0);

    return tot_bytes;
}

void *send_loop(void *data){
    uint64_t tot_bytes = 0;
    struct thread_info *thread_info = (struct thread_info *) data;
    fprintf(stdout, "Started thread\n");

    while(1){
        pthread_mutex_lock(&(thread_info->new_session_mutex));
        if(thread_info->status == PAUSED)
            pthread_cond_wait(&(thread_info->new_session), 
                    &(thread_info->new_session_mutex));
        pthread_mutex_unlock(&(thread_info->new_session_mutex));

        //Sanity
        assert(thread_info->status == RUNNING);

        tot_bytes = generate_udp_traffic(thread_info); 

       
        fprintf(stdout, "Done with sending. Sent %lu bytes\n", tot_bytes);
        //Send end session
        thread_info->status = PAUSED;
    }
}

void network_event_loop(int32_t udp_sock_fd){
    fd_set recv_set, recv_set_copy;
    int32_t fdmax = udp_sock_fd  + 1;
    int32_t retval = 0, i;
    ssize_t numbytes = 0;
    int32_t recv_socket = 0;
    struct pkt_hdr *hdr;
    struct new_session_pkt *new_s_pkt;

    pthread_t threads[NUM_THREADS];
    struct thread_info *thread_infos[NUM_THREADS];

    struct msghdr msg;
    struct iovec iov;
    uint8_t buf[MAX_PAYLOAD_LEN] = {0};
    struct sockaddr_storage sender_addr;
    socklen_t addr_len;
    char addr_presentation[INET6_ADDRSTRLEN];
    uint16_t recv_port = 0;
    struct data_pkt *pkt = NULL;

    //Initialize and start threads used to generate traffic
    for(i = 0; i<NUM_THREADS; i++){
        thread_infos[i] = (struct thread_info*) 
            malloc(sizeof(struct thread_info));
        thread_infos[i]->status = PAUSED;
        thread_infos[i]->udp_sock_fd = udp_sock_fd;
        pthread_cond_init(&(thread_infos[i]->new_session), NULL);
        pthread_mutex_init(&(thread_infos[i]->new_session_mutex), NULL);

        pthread_create(&threads[i], NULL, send_loop, (void *) thread_infos[i]);
    }

    memset(&msg, 0, sizeof(struct msghdr));
    memset(&iov, 0, sizeof(struct iovec));
    memset(&sender_addr, 0, sizeof(struct sockaddr_storage));

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    msg.msg_name = (void *) &sender_addr;

    //Unlike for example recvfrom, this one seems to be left unchanged
    msg.msg_namelen = sizeof(struct sockaddr_storage);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    FD_ZERO(&recv_set);
    FD_ZERO(&recv_set_copy);
    FD_SET(udp_sock_fd, &recv_set);

    while(1){
        //Use this an indication for wheter or not new session is using TCP
        recv_socket = 0;
        recv_set_copy = recv_set;
        retval = select(fdmax, &recv_set_copy, NULL, NULL, NULL);

        if(retval < 0){
            fprintf(stdout, "Select failed, aborting\n");
            close(udp_sock_fd);
            exit(EXIT_FAILURE);
        } else {
                numbytes = recvmsg(udp_sock_fd, &msg, 0);
                hdr = (struct pkt_hdr *) buf;

                if(hdr->type != NEW_SESSION){
                    fprintf(stdout, "Received an incorrect packet\n");
                    continue;
                }
            

            //The combination adress + port is used for lookup in the currently
            //running threads
            if(sender_addr.ss_family == AF_INET){
                inet_ntop(AF_INET, 
                        &(((struct sockaddr_in *) &sender_addr)->sin_addr),
                        addr_presentation, INET6_ADDRSTRLEN);
                recv_port = 
                    ntohs(((struct sockaddr_in *) &sender_addr)->sin_port);
            } else if(sender_addr.ss_family == AF_INET6){
                inet_ntop(AF_INET6, 
                        &(((struct sockaddr_in6 *) &sender_addr)->sin6_addr),
                        addr_presentation, INET6_ADDRSTRLEN);
                recv_port = 
                    ntohs(((struct sockaddr_in6 *) &sender_addr)->sin6_port);
            }

            //Check that I have not already started the thread belonging to
            //this session
            //The checks in this and the next for-loop are not thread-safe,
            //but it doesn't matter as the behvaior is not critical.
            //Wors-case, the session request will be accepted on the next
            //request
            for(i = 0; i<NUM_THREADS; i++){
                if(thread_infos[i]->status == RUNNING && 
                        thread_infos[i]->source.ss_family == 
                        sender_addr.ss_family){
                    if(sender_addr.ss_family == AF_INET &&
                            !memcmp(&sender_addr, &thread_infos[i]->source,
                            sizeof(struct sockaddr_in))){
                        break;
                    } else if(sender_addr.ss_family == AF_INET6 &&
                            !memcmp(&sender_addr, &thread_infos[i]->source,
                            sizeof(struct sockaddr_in6))){
                        break;
                    }
                }
            }

            if(i!=NUM_THREADS){
                fprintf(stdout, "This session has already been seen (%s:%d)\n", 
                        addr_presentation, recv_port);
                continue;
            }

            for(i=0; i<NUM_THREADS; i++){
                //Found an availale thread. Initialise and start
                if(thread_infos[i]->status == PAUSED){
                   new_s_pkt = (struct new_session_pkt *) buf;
                   thread_infos[i]->num_packets = new_s_pkt->num_packets;
                   thread_infos[i]->num_bursts = new_s_pkt->num_bursts;
                   thread_infos[i]->payload_len = new_s_pkt->payload_len;
                    

                    memcpy(&thread_infos[i]->source, &sender_addr, 
                            sizeof(struct sockaddr_storage));
                    
                    //Signal thread that it has work to do
                    pthread_mutex_lock(&(thread_infos[i]->new_session_mutex));
                    thread_infos[i]->remote_port = recv_port;
                    thread_infos[i]->status = RUNNING;
                    pthread_cond_signal(&(thread_infos[i]->new_session));
                    pthread_mutex_unlock(&(thread_infos[i]->new_session_mutex));
                    fprintf(stdout, "Created a new session for %s:%d\n", 
                            addr_presentation, recv_port);
                    break;
                }
            }

            if(i==NUM_THREADS){
                fprintf(stdout, "No available threads\n");

                //Create SENDER_FULL packet, which only consis of 1 byte payload
                buf[0] = SENDER_FULL;
                iov.iov_len = 1;

                if(recv_socket > 0){
                    close(recv_socket);
                } else
                    sendmsg(udp_sock_fd, &msg, 0);

                iov.iov_len = sizeof(buf);
                //Send message that sender is full
            }
        }
    }
}

void usage(){
    fprintf(stdout, "Supported command line arguments (all required)\n");
    fprintf(stdout, "-s : Source IP to bind to\n");
    fprintf(stdout, "-p : Source port\n");
}

int main(int argc, char *argv[]){
    char *src_ip = NULL, *src_port = NULL;
    int32_t c;
    int32_t udp_sock_fd;

    if(argc != 5){
        usage();
        exit(EXIT_FAILURE);
    }

    while((c = getopt(argc, argv, "s:p:")) != -1){
        switch(c){
            case 's':
                src_ip = optarg;
                break;
            case 'p':
                src_port = optarg;
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
        }
    }

    if(src_ip == NULL || src_port == NULL){
        usage();
        exit(EXIT_FAILURE);
    } else {
        fprintf(stdout, "Source IP: %s:%s\n", src_ip, src_port);
    }

    if((udp_sock_fd = bind_local(src_ip, src_port, SOCK_DGRAM, 0)) == -1){
        fprintf(stdout, "Could not create UDP socket\n");
        exit(EXIT_FAILURE);
    }


    network_event_loop(udp_sock_fd);
    return 0;
}
