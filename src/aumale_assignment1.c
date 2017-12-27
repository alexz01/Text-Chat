/**
 * @aumale_assignment1
 * @author  Alexander Umale <aumale@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * This contains the main function. Add further description here....
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ifaddrs.h>

#include "../include/global.h"
#include "../include/logger.h"

#define UBITNAME "aumale"
#define BACKLOG 10     // how many pending connections queue will hold

struct client_stat{
	char cl_IP[INET6_ADDRSTRLEN];
	char cl_hostname[35];
	int cl_port;//[5];
	int cl_loggedin;
	int cl_sent;
	int cl_received;
	char cl_blocked[10*INET6_ADDRSTRLEN];	
	struct client_stat *cl_next;
};

struct msg_buffer{
	//clients are identified by IP only and not by process
	int active;
	char src[INET6_ADDRSTRLEN];
	char dest[INET6_ADDRSTRLEN];
	char msg[256];
};

//function prototypes
void sigchld_handler(int);
void *get_in_addr(struct sockaddr *);
int get_eth0_ip(char * eth0_ip);
int cl_list_add(struct sockaddr_storage , char *, struct client_stat **,struct client_stat **);
int cl_list_rem(struct sockaddr_storage *,struct client_stat **,struct client_stat **);
int cl_list_upd(struct sockaddr_storage *,struct client_stat *,struct client_stat *, char param, int val);
int cl_list_srch(struct sockaddr_storage *, struct client_stat *, struct client_stat *, struct client_stat *);
int cl_list_to_str(struct client_stat **, char **);
int sort_list(struct client_stat **);


/**
 * main function
 *
 * @param  argc Number of arguments
 * @param  argv The argument list
 * @return 0 EXIT_SUCCESS
 */
int main(int argc, char **argv)
{
	/*Init. Logger*/
	cse4589_init_log(argv[2]);

	/*Clear LOGFILE*/
	fclose(fopen(LOGFILE, "w"));

	/*Start Here*/
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size = sizeof their_addr;
    struct sigaction sa;
    int yes=1;
	int is_logged = 0;
    char ip_addr[INET6_ADDRSTRLEN];
	char *port = "65533";
    int status;
    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax = 0, i;        // maximum file descriptor number
	char input[300],input_bk[300], *command;
	char message[300], message_bk[300], *msg_cmd;
    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);
	int message_size;
	struct client_stat *start, *end;
	char *list_str = malloc(1000*sizeof(char));
	struct msg_buffer buffer[100];
	list_str[0] = '\0';
	if(argc < 3) {
	//	printf("<3");
		cse4589_print_and_log("[%s:ERROR]\n", argv[0]);
		cse4589_print_and_log("[%s:END]\n", argv[0]);
		return 0;
	} 
	//printf("%d %s",atoi(argv[2]),argv[1]);
	if(atoi(argv[2])<1025){
		
		//used reserved port
		cse4589_print_and_log("[%s:ERROR]\n", argv[0]);
		cse4589_print_and_log("[%s:END]\n", argv[0]);
		return 0;
	} else {
		port = argv[2];
	}
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	if(strcmp(argv[1],"s") == 0 ){
		//started in server mode
		
		start = end = NULL;	
		
		hints.ai_flags = AI_PASSIVE; // use my IP

		// get ip_address of first eth interface
		if ((status = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
			return 1;
		}
	
		// loop through all the results and bind to the first we can
		for(p = servinfo; p != NULL; p = p->ai_next) {
			if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
				perror("server: socket");
				continue;
			}
			//check if port is free if not then wait for it to be free
			if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
				perror("setsockopt");
				exit(1);
			}
			//bind the port with socket
			if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
				close(sockfd);
				perror("server: bind");
				continue;
			}	
			break;
		}
		
		freeaddrinfo(servinfo); // all done with this structure
	
		if (p == NULL)  {
			fprintf(stderr, "server: failed to bind\n");
			exit(1);
		}
	
		if (listen(sockfd, BACKLOG) == -1) {
			perror("listen");
			exit(1);
		}
	
		sa.sa_handler = sigchld_handler; // reap all dead processes
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		if (sigaction(SIGCHLD, &sa, NULL) == -1) {
			perror("sigaction");
			exit(1);
		}
		
		//printf("server: waiting for connections...@%s\n",PORT);
		FD_SET(sockfd, &master);
		if(sockfd > fdmax) 
			fdmax = sockfd;
	
	    FD_SET(STDIN_FILENO,&master);
		
		while(1) {
			fputs("[PA1]$ ",stdout);
			fflush(stdout);
			read_fds = master; // copy it
			if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
				perror("select");
				exit(4);
			}
			for(i = 0; i <= fdmax; i += 1){
				if(FD_ISSET(i,&read_fds)){
					if(i == STDIN_FILENO){
						fgets(input,sizeof input, stdin);
						if(strlen(input) == 1) break;
						input[strlen(input)-1] = '\0';
						strcpy(input_bk,input);
						command = strtok(input," ");
						//printf("command entered: %s\n",command);
						// do something with the command received
						if(strcmp(command,"IP") == 0) { 
							if(get_eth0_ip(ip_addr) != 0) {
								perror("get_eth0_ip");
							}else{
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("IP:%s\n",ip_addr);						
							}						
						} else if(strcmp(command,"PORT") == 0) {
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("PORT:%s\n",port);	
						} else if(strcmp(command,"AUTHOR") == 0) {
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("I, %s, have read and understood the course academic integrity policy.\n",UBITNAME);		
						} else if(strcmp(command,"LIST") == 0) {
							struct client_stat *ptr = start;
							int i =1;
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							while(ptr != NULL){
								char new_str[300];
								if(ptr->cl_loggedin == 0 ){
									ptr = ptr->cl_next;
									continue;
								}
								cse4589_print_and_log("%-5d%-35s%-20s%-8i\n",i,ptr->cl_hostname, ptr->cl_IP,ptr->cl_port);
//							//	printf("%-5d%-35s%-20s%-8s;\n", i, ptr->cl_hostname, ptr->cl_IP, ptr->cl_port);
								i += 1;
								if(ptr->cl_loggedin)
									strcat(list_str,new_str);							
								ptr = ptr->cl_next;
							}
							
							//printf("[%s:ERROR] \nTO BE IMPLEMENTED: LIST COMMAND\n",command);
						} else if(strcmp(command,"FD") == 0) {
							int fds_itr = 0;
							struct sockaddr_storage peer_addr;
							for (fds_itr = 0 ; fds_itr <=fdmax;fds_itr++){
								if(FD_ISSET(fds_itr,&master)&& fds_itr != STDIN_FILENO && fds_itr != sockfd){
									if(getpeername(fds_itr, (struct sockaddr*)&peer_addr, (socklen_t*)&sin_size)==-1){
									//	printf("error on %d",fds_itr);fflush(stdout);
										perror("getpeername");
									}
									else{
								//	printf("%d : %s\n",fds_itr, inet_ntoa(((struct sockaddr_in*)&peer_addr)->sin_addr));
									}
									
								}else {
										
										//printf("else %d\n",fds_itr);
								}
								
							}

						} else if(strcmp(command,"STATISTICS") == 0) {
							struct client_stat *ptr = start;
							int i =1;
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							while(ptr != NULL){
							char new_str[300];
							cse4589_print_and_log("%-5d%-35s%-8d%-8d%-8s\n",i,ptr->cl_hostname, ptr->cl_sent,ptr->cl_received, ptr->cl_loggedin == 1? "logged-in": "logged-out" );
//						//	printf("%-5d%-35s%-20s%-8s;\n", i, ptr->cl_hostname, ptr->cl_IP, ptr->cl_port);
							i += 1;
							if(ptr->cl_loggedin)
								strcat(list_str,new_str);							
							ptr = ptr->cl_next;
							}
						} else if(strcmp(command,"BLOCKED") == 0) {
							struct client_stat *ptr = start;
							int i =1;
							char *blocker_IP = strtok(NULL," ");
						//	printf("blocked ip: %s",blocker_IP);
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							char new_str[300];
							new_str[0] = '\0';
							while(ptr != NULL){
								
								if(strcmp(ptr->cl_IP,blocker_IP) == 0){
									strcpy(new_str,ptr->cl_blocked);
									char * ips = strtok(new_str,";");
									while(ips != NULL){
										struct client_stat *blocks = start;
										while(blocks != NULL){
											if(strcmp(blocks->cl_IP,ips) == 0 ){
												cse4589_print_and_log("%-5d%-35s%-20s%-8i\n",i,blocks->cl_hostname, blocks->cl_IP,blocks->cl_port);
												break;
											}
											blocks = blocks->cl_next;
										}
										
										
										ips = strtok(NULL,";");;
									}
									break;
								}
								
								
//							//	printf("%-5d%-35s%-20s%-8s;\n", i, ptr->cl_hostname, ptr->cl_IP, ptr->cl_port);
								i += 1;
								if(ptr->cl_loggedin)
									strcat(list_str,new_str);							
								ptr = ptr->cl_next;
							}

						} else { // command not in server or general command list				
							cse4589_print_and_log("[%s:ERROR]\n", command);
							
						}
						cse4589_print_and_log("[%s:END]\n", command);


						
						
					} else if(i == sockfd){ // is a server process
						//Responds to LOGIN command
						if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size))== -1) {
							perror("accept");
							continue;
						}
					//	printf("%i ",new_fd);
						//add new socket to select list of read FDs
						//getpeername(i, (struct sockaddr*)&their_addr, (socklen_t*)&sin_size);
					//	printf("new client %s connected\n",inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr) );
						FD_SET(new_fd, &master);
						if(new_fd > fdmax)
							fdmax = new_fd;
						
						/*TODO :
							implement : receive port from client
							implement : add new connection to the connected client list -- DONE
							implement : send list of active users(logged/not logged)
							implement : send all buffered messages for 
														
						*/
						char client_port[5];
						int rec = 0;
						
						//read_fds = master;
						//if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
						//    perror("select");
						//    exit(4);
						//}
						
						if((rec = recv(new_fd,client_port,5,0)) == -1){
							perror("error receive");
						}else{
						//printf("bytes received: %d ",rec);
						//printf("port received: %s ",client_port);
						}
						//fflush(stdout);
//
						struct client_stat *ptr = start;
						int inlist = 0;
						//printf("[%s:SUCCESS]\n",command);
						while(ptr != NULL){
							if(strcmp(inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr),ptr->cl_IP) ==0){
								inlist = 1;
								break;
							}
							ptr = ptr->cl_next;
						}
						if(inlist){
							ptr->cl_loggedin = 1;
						}
						else{
							struct client_stat *tmp =  malloc(sizeof (struct client_stat));
							if (tmp == NULL) {perror("malloc");};
							strcpy(tmp->cl_IP,inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr)) ;
							//printf("%s", tmp->cl_IP);
							//struct hostent *ent = gethostbyaddr(tmp->cl_IP,sizeof(tmp->cl_IP),((struct sockaddr_in*)&their_addr)->sin_family);
							//
							char host[40];
							int err = 0;
							if((err = getnameinfo((struct sockaddr*)&their_addr,sizeof(their_addr),host,40,NULL,5,0) !=0)){ perror("name error");};
							//printf("%s : %s",host, client_port);
							fflush(stdout);
							strcpy(tmp->cl_hostname, host);
							tmp->cl_port = atoi(client_port);
							//sprintf(tmp->cl_port,"%s",client_port);
							//printf("\n%d and %s\n",tmp->cl_port,client_port);
							tmp->cl_loggedin = 1;
							tmp->cl_sent = 0;
							tmp->cl_received = 0;
							tmp->cl_blocked[0] = '\0';
							tmp->cl_next = NULL;	
//						//	printf("%d\n", tmp->cl_next);
							fflush(stdout);
							if(start == NULL){
								start = tmp;
								end = start;
							}else{
								end->cl_next = tmp;
								end = tmp;
							}
						}
						list_str[0] = '\0';
						
						//sort the list here//////////////////////////////////////////////////////////////////////////
						//sort_list(&start);
						
						////////////////////////////////////////////////////////////////////////////////////////////////
						
						cl_list_to_str(&start,&list_str);
						char * strptr = list_str;
						//strptr[strlen(list_str)] = '\0';
						//printf("\nlist in string: %lu %s .",strlen(strptr),strptr);
						int sent  = 0;
						//printf("list send");
						//printf("addr: %s",client_list->cl_IP);
						fflush(stdout);
						
						while(strlen(strptr)>0){
							if((sent = send(new_fd,strptr,strlen(strptr),0))==-1){
								perror("send failed");
								break;
							}
							strptr = strptr + sent ;
							//printf("sending : %d",sent);
							fflush(stdout);
						}
					//	printf("reached");fflush(stdout);
						
					}else {
						memset(&message, 0, sizeof(message));
						message[0] ='\0';
						getpeername(i, (struct sockaddr*)&their_addr, (socklen_t*)&sin_size);
						if((message_size = recv(i,message,300,0)) <= 0){
								// some error or close() received
							if(message_size == 0){
							//	printf("client %s closed connection\n",inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr));
								close(i);
								FD_CLR(i,&master);
								
								//TODO : implement logout code here

								struct client_stat *ptr = start;
								int inlist = 0;
								//printf("[%s:SUCCESS]\n",command);
								while(ptr != NULL){
									if(strcmp(inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr),ptr->cl_IP) ==0){
										inlist = 1;
										break;
									}
									ptr = ptr->cl_next;
								}
								if(inlist){
									ptr->cl_loggedin = 0;
								}



								
								
							}
						} else {
							// message received		
							//printf("msg: %s",message);
							strcpy(message_bk,message);
							message_bk[message_size+1] = '\0';							
						//	printf("message from %i %s %lu: %s\n",i,inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr),strlen(message_bk),message_bk);
							msg_cmd = strtok(message, " ");
						//	printf("command received: \"%s\"\n",msg_cmd);
							
							
							if(strcmp(msg_cmd,"REFRESH") == 0) {
							//	printf("refresh received from %s:%s",message_bk, inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr) );
								list_str[0]= '\0';
								
								//sort the list
								//sort_list(&start);
								
								
								cl_list_to_str(&start,&list_str);
								
								char * strptr = list_str;
								//strptr[strlen(list_str)] = '\0';
								//printf("\nlist in string: %lu %s .",strlen(strptr),strptr);
								int sent  = 0;
							//	printf("");
								//printf("addr: %s",client_list->cl_IP);
								fflush(stdout);
								
								while(strlen(strptr)>0){
									if((sent = send(i,strptr,strlen(strptr),0))==-1){
										perror("send failed");
										break;
									}
									strptr = strptr + sent ;
								//	printf("sending : %d",sent);
									fflush(stdout);
								}
								
							} else if(strcmp(msg_cmd,"SEND") == 0) {
								char *msg = msg_cmd;
								//printf("msg_cmd: %s",msg_cmd);
								msg = msg + strlen(msg_cmd) + 1;
								//printf("msg_cmd: %s",msg_cmd);
								char * dest_IP = msg_cmd = strtok(NULL," ");
								//printf("msg_dest: %s", msg_cmd);
								msg = msg + strlen(msg_cmd) +1;
								//printf("msg : %s", msg);
								char *src = inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr);
								struct client_stat *ptr = start;
								
								//printf("[%s:SUCCESS]\n",command);
								while(ptr != NULL){
									
									if( strcmp(ptr->cl_IP,dest_IP) == 0 ){
										// TODO send message to dest
										//TODO add code for blocked
										int blocked = 0;	
										char *block_str = malloc(strlen(ptr->cl_blocked));
										strcpy(block_str,ptr->cl_blocked);
										char * blocked_ip = strtok(block_str,";");
										while(blocked_ip != NULL){
											if(strcmp(blocked_ip,dest_IP) == 0){
												blocked = 1;
												break;
											}												
											blocked_ip = strtok(NULL,";");
										}
										if(blocked==0){
											if(ptr->cl_loggedin == 1){
//												int soc = 0;
												
												
												int fds_itr = 0;
												struct sockaddr_storage peer_addr;
												for (fds_itr = 0 ; fds_itr <=fdmax;fds_itr++){
													if(FD_ISSET(fds_itr,&master)&& fds_itr != STDIN_FILENO && fds_itr != sockfd){
														if(getpeername(fds_itr, (struct sockaddr*)&peer_addr, (socklen_t*)&sin_size)==-1){
														//	printf("error on %d",fds_itr);fflush(stdout);
															perror("getpeername");
														}
														else{
													//	printf("%d : %s\n",fds_itr, inet_ntoa(((struct sockaddr_in*)&peer_addr)->sin_addr));
															if(strcmp(dest_IP,inet_ntoa(((struct sockaddr_in*)&peer_addr)->sin_addr))== 0){
															//	printf("sending to %d\n",fds_itr);
																char msg_formatted[300];
														    	sprintf(msg_formatted,"msg from:%s\n[msg]:%s\n",src,msg);
														    	int sent=0;
														    	if((sent = send(fds_itr	,msg_formatted,strlen(msg_formatted),0))== -1){
														    		perror("error login send");
														    		fflush(stdout);
														    	}
														    	//printf ("sent %d: %s",sent,msg_formatted);
														    	//break;
															}
														}
														
													}else {
															
															//printf("else %d\n",fds_itr);
													}
												
												}
												

												/*for(soc = 0; soc <=fdmax;soc++){
													struct sockaddr_storage dest_addr ;
													if(soc == STDIN_FILENO || soc == sockfd) continue;
													
													getpeername(soc, (struct sockaddr*)&dest_addr, (socklen_t*)&sin_size);
													
												//	printf("before sending: %d %s\n",soc,inet_ntoa(((struct sockaddr_in*)&dest_addr)->sin_addr));
													
													if(strcmp(dest_IP,inet_ntoa(((struct sockaddr_in*)&dest_addr)->sin_addr))== 0){
													//	printf("sending to %d\n",soc);
														char msg_formatted[300];
														sprintf(msg_formatted,"msg from:%s\n[msg]:%s\n",dest_IP,msg);
														int sent=0;
														if((sent = send(soc	,msg_formatted,strlen(msg_formatted),0))== -1){
															perror("error login send");
															fflush(stdout);
														}
													//	printf ("sent %d: %s",sent,msg_formatted);
														//break;
													}
												}*/
												
												
												// increase the received by destination by 1
												ptr->cl_received += 1;
												
												
												cse4589_print_and_log("[RELAYED:SUCCESS]\n" );
												cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", src, dest_IP, msg);
												cse4589_print_and_log("[RELAYED:END]\n" );
											} else {
												// add to buffer;
												int i;
												for(i = 0; i <100; i++){
													if(buffer[i].active == 0){
														buffer[i].active = 1;
														strcpy(buffer[i].src,src);
														strcpy(buffer[i].dest,dest_IP);
														strcpy(buffer[i].msg,msg);
														break;
													}
												}
											}
										}
										//break;
									}if( strcmp(ptr->cl_IP,src) == 0 ){
										ptr->cl_sent += 1;
									}														
									ptr = ptr->cl_next;
								}
								
								
								
								
								
							} 
							else if(strcmp(msg_cmd,"BROADCAST") == 0) {
							//	printf("inside %s",msg_cmd);
								char *msg = msg_cmd;
								//printf("msg_cmd: %s",msg_cmd);
								msg = msg + strlen(msg_cmd) + 1;
								//printf("msg_cmd: %s",msg_cmd);
								char * dest_IP = "255.255.255.255";
								//printf("msg_dest: %s", msg_cmd);
								//msg = msg + strlen(msg_cmd) +1;
								//printf("msg : %s", msg);
								char *src = inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr);
								struct client_stat *ptr = start;
								
								//printf("[%s:SUCCESS]\n",command);
								while(ptr != NULL){
									
									if(1){
										// TODO send message to dest
										//TODO add code for blocked
										int blocked = 0;	
										char *block_str = malloc(strlen(ptr->cl_blocked));
										strcpy(block_str,ptr->cl_blocked);
										char * blocked_ip = strtok(block_str,";");
										while(blocked_ip != NULL){
											if(strcmp(blocked_ip,dest_IP) == 0){
												blocked = 1;
												break;
											}												
											blocked_ip = strtok(NULL,";");
										}
										if(blocked==0){
											if(ptr->cl_loggedin == 1){
//												int soc = 0;
												
												
												int fds_itr = 0;
												struct sockaddr_storage peer_addr;
												for (fds_itr = 0 ; fds_itr <=fdmax;fds_itr++){
													if(FD_ISSET(fds_itr,&master)&& fds_itr != STDIN_FILENO && fds_itr != sockfd){
														if(getpeername(fds_itr, (struct sockaddr*)&peer_addr, (socklen_t*)&sin_size)==-1){
														//	printf("error on %d",fds_itr);fflush(stdout);
															perror("getpeername");
														}
														else{
													//	printf("%d : %s\n",fds_itr, inet_ntoa(((struct sockaddr_in*)&peer_addr)->sin_addr));
															if(strcmp(src,inet_ntoa(((struct sockaddr_in*)&peer_addr)->sin_addr))!= 0){
															//	printf("sending to %d\n",fds_itr);
																char msg_formatted[300];
														    	sprintf(msg_formatted,"msg from:%s\n[msg]:%s\n",src,msg);
														    	int sent=0;
														    	if((sent = send(fds_itr	,msg_formatted,strlen(msg_formatted),0))== -1){
														    		perror("error login send");
														    		fflush(stdout);
														    	}
														    //	printf ("sent %d: %s",sent,msg_formatted);
														    	//break;
															}
														}
														
													}else {
															
															//cse4589_print_and_log("else %d\n",fds_itr);
													}
												
												}
												ptr->cl_received += 1;												
												cse4589_print_and_log("[RELAYED:SUCCESS]\n" );
												cse4589_print_and_log("msg from:%s, to:%s\n[msg]:%s\n", src, dest_IP, msg);
												cse4589_print_and_log("[RELAYED:END]\n" );
											} else {
												// add to buffer;
												int i;
												for(i = 0; i <100; i++){
													if(buffer[i].active == 0){
														buffer[i].active = 1;
														strcpy(buffer[i].src,src);
														strcpy(buffer[i].dest,dest_IP);
														strcpy(buffer[i].msg,msg);
														break;
													}
												}
											}
										}
										//break;
									}if( strcmp(ptr->cl_IP,src) == 0 ){
										ptr->cl_sent += 1;
									}														
									ptr = ptr->cl_next;
								}
								
								
							} else if(strcmp(msg_cmd,"BLOCK") == 0) {
							//	printf("inside %s",msg_cmd);
								struct sockaddr_storage peer_addr;
								if(getpeername(i, (struct sockaddr*)&peer_addr, (socklen_t*)&sin_size)==-1){
								//	printf("error on %d",i);fflush(stdout);
									perror("getpeername");
								}
								char * block = strtok(NULL," ");
								char *blocker_IP = inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr);
								
								struct client_stat *ptr = start;
								char new_str[300];
								memset(new_str,0,sizeof(new_str));
								
								int inlist = 0;
								while(ptr != NULL){
									
									if(strcmp(ptr->cl_IP,blocker_IP) == 0){
										strcpy(new_str,ptr->cl_blocked);								
										char *blocked = strtok(new_str,";");
										while(blocked!=NULL){ // check if it is already present before appending at end
											if(strcmp(blocked,block)==0) // already in list
												inlist = 1;
											blocked = strtok(NULL,";");
										}
										if(inlist ==0){
											memset(new_str,0,300);
											strcpy(new_str,ptr->cl_blocked);
											sprintf(ptr->cl_blocked,"%s;%s",new_str,blocked);
										}
									}									
									ptr = ptr->cl_next;
								}

							} else if(strcmp(msg_cmd,"UNBLOCK") == 0) {
							//	printf("inside %s",msg_cmd);
								struct sockaddr_storage peer_addr;
								if(getpeername(i, (struct sockaddr*)&peer_addr, (socklen_t*)&sin_size)==-1){
								//	printf("error on %d",i);fflush(stdout);
									perror("getpeername");
								}
								char * block = strtok(NULL," ");
								char *blocker_IP = inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr);
								
								struct client_stat *ptr = start;
								char new_str[300];
								char temp[300];
								memset(new_str,0,sizeof(new_str));
								memset(temp,0,sizeof(new_str));
								//strcpy(new_str,ptr->cl_blocked);
								//strcpy(temp,ptr->cl_blocked);								
					
								while(ptr != NULL){
									
									if(strcmp(ptr->cl_IP,blocker_IP) == 0){
										char *blocked = strtok(ptr->cl_blocked,";");
										while(blocked!=NULL){ // check if it is already present before appending at end
											if(strcmp(blocked,block)==0) // already in list
												continue;
											sprintf(temp,"%s;%s",new_str,blocked);
											strcpy(new_str,temp);
											blocked = strtok(NULL,";");
										}
										
									}									
									ptr = ptr->cl_next;
								}
								
								
								
								
							} else if(strcmp(msg_cmd,"EXIT") == 0) {
								struct client_stat *ptr = start;
								struct client_stat *prev = start;

								while(ptr != NULL){
									if(strcmp(ptr->cl_IP, inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr)) == 0){
										if(ptr == start){
											if(start->cl_next == NULL){ //only one item in list
												start = NULL;
												free(ptr);
											} else { // has more item
												start = start->cl_next;
											}
											break;
										}else{
											prev->cl_next = ptr->cl_next;
											free(ptr);
											break;
										}
									}
									prev = ptr;
									ptr = ptr->cl_next;
								}							
							}
						}						
					}	
				}
			}			
			
		}

	}else if(strcmp(argv[1],"c") == 0 ){
		// Entered in client mode. No automatic setup required
		// initialize user list to be added on login
		char * clients_list = malloc(1000*sizeof(char));
		FD_SET(STDIN_FILENO,&master);
		while(1) {  // main accept() loop
			fputs("[PA1]$ ",stdout);
			fflush(stdout);
			read_fds = master; // copy it
			if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
				perror("select");
				exit(4);
			}
	        for(i = 0; i <= fdmax; i += 1){
				if(FD_ISSET(i,&read_fds)){
					if(i == STDIN_FILENO){
						fgets(input,sizeof input, stdin);
						if(strlen(input) == 1) break;
						input[strlen(input)-1] = '\0';
						strcpy(input_bk,input);
						input_bk[strlen(input_bk)] = '\0';
						command = strtok(input," ");
						//printf("command entered: %s\n",input_bk);
						// do something with the command received
						if(strcmp(command,"IP") == 0) { 
							if(get_eth0_ip(ip_addr) != 0) {
								perror("get_eth0_ip");
							}else{
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("IP:%s\n",ip_addr);						
							}
							
						} else if(strcmp(command,"PORT") == 0) {
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("PORT:%s\n",port);	
							
						} else if(strcmp(command,"AUTHOR") == 0) {
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("I, %s, have read and understood the course academic integrity policy.\n",UBITNAME);		
							
						} else if(strcmp(command,"LIST") == 0) {
							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("%s",clients_list);
							//printf("[%s:ERROR] \nTO BE IMPLEMENTED: LIST COMMAND\n",command);
						
						} else if(strcmp(command,"LOGIN") == 0 && is_logged ==0 ) {
							status = 0;
							// 
							//no message is sent as login will implicitly send message to server
							
							clients_list[0] = '\0';
							char * rmt_ip, *rmt_port;
							rmt_ip = strtok(NULL," ");
							rmt_port = strtok(NULL," ");
							//if(rmt_ip == NULL || strcmp(rmt_ip,"") == 0 || rmt_port == NULL || (rmt_port,"")) goto fail;
							//printf("%s%s:%s,%lu %lu %lu %lu",command, rmt_ip,rmt_port,strlen(input_bk),strlen(command),strlen(rmt_ip),strlen(rmt_port));
							fflush(stdout);
							if ((status = getaddrinfo(rmt_ip, rmt_port, &hints, &servinfo)) != 0) {
								
								fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
								return 1;
							}
						    
							// loop through all the results and connect to the first we can
							for(p = servinfo; p != NULL; p = p->ai_next) {
								if ((sockfd = socket(p->ai_family, p->ai_socktype,
										p->ai_protocol)) == -1) {
									perror("client: socket");
									continue;
								}
						    
								if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
									close(sockfd);
									perror("client: connect");
									continue;
								}
								FD_SET(sockfd,&master);
								if(sockfd>fdmax)
									fdmax = sockfd;
								break;
							}
						    
							if (p == NULL) {
								fprintf(stderr, "client: failed to connect\n");
								status = -1;
							}
							if(status == 0){
								//connected successfully
								/*TODO:
									received list of registered users
									received all buffered messages
								*/
								int sent =0;
								
								if((sent = send(sockfd,port,strlen(port),0))== -1){
									perror("error login send");
									fflush(stdout);
								}
								//printf("bytes sent: %d",sent);
								sent = 0;
								
								//printf("sizeof(list_str): %lu %lu", sizeof(list_str),strlen(list_str));
								
								if((sent = recv(sockfd,list_str,1000,0)) == -1){
								//	printf("error receiving client list");
									goto fail; 
								}
								clients_list[0] ='\0';
 								//printf("bytes received : %d ",sent);
								//printf("%s\n",list_str );
								//char *list_bk = malloc(1000*sizeof(char));
								//char *list_bk_ptr = list_bk;
								//strcpy(list_bk,list_str);
//								
								char *list_line = strtok(list_str," ");
								char str_tmp[50];
								while(list_line != NULL){
									//int size = strlen(list_line) +1 ;
									char *list_num = list_line;
									char *list_host = strtok(NULL, " ;"); 
									char *list_IP = strtok(NULL, " ;"); 
									char *list_port = strtok(NULL, " ;");
									
									//strcpy(list_str,list_bk_ptr);
									sprintf(str_tmp, "%-5s%-35s%-20s%-8s\n", list_num, list_host, list_IP, list_port);
									//printf(":%s",str_tmp);
									strcat(clients_list,str_tmp);
									//list_str_ptr += size;
									list_line = strtok(NULL," ;");								
								}
//							//	printf("clientlist:\n%s",clients_list);
								
								is_logged = 1;
								
								//FD_SET(sockfd,&master);
								
								cse4589_print_and_log("[%s:SUCCESS]\n",command);
							}else{
								cse4589_print_and_log("[%s:ERROR]\n", command);
							}
							
						} else if (strcmp(command,"EXIT") == 0) {
							//TODO send message to delete from client list
							if((send(sockfd,command,strlen(command),0))== -1){
								perror("error login send");
								fflush(stdout);
							}
							is_logged = 0;

							cse4589_print_and_log("[%s:SUCCESS]\n",command);
							cse4589_print_and_log("[%s:END]\n",command);
							goto end;
							
						}else if(is_logged) {
							
							if(strcmp(command,"REFRESH") == 0) {	
								//TODO implement Refresh
								int sent =0;
								
								if((sent = send(sockfd,command,strlen(command),0))== -1){
									perror("error login send");
									fflush(stdout);
								}
							//	printf("sent %d",sent);fflush(stdout);
								sent = 0;
								
								//printf("sizeof(list_str): %lu %lu", sizeof(list_str),strlen(list_str));
								if((sent = recv(sockfd,list_str,1000,0)) == -1){
								//	printf("error receiving client list");
									goto fail; 
								}
								clients_list[0] ='\0';
 								//printf("bytes received : %d ",sent);
								//printf("%s\n",list_str );
								//char *list_bk = malloc(1000*sizeof(char));
								//char *list_bk_ptr = list_bk;
								//strcpy(list_bk,list_str);
//								
								char *list_line = strtok(list_str," ");
								char str_tmp[50];
								while(list_line != NULL){
									//int size = strlen(list_line) +1 ;
									char *list_num = list_line;
									char *list_host = strtok(NULL, " ;"); 
									char *list_IP = strtok(NULL, " ;"); 
									char *list_port = strtok(NULL, " ;");
									
									//strcpy(list_str,list_bk_ptr);
									sprintf(str_tmp, "%-5s%-35s%-20s%-8s\n", list_num, list_host, list_IP, list_port);
									//printf(":%s",str_tmp);
									strcat(clients_list,str_tmp);
									//list_str_ptr += size;
									list_line = strtok(NULL," ;");								
								}
//							//	printf("clientlist:\n%s",clients_list);
								
								cse4589_print_and_log("[%s:SUCCESS]\n",command);
								cse4589_print_and_log("%s",clients_list);
							}
							else if(strcmp(command,"SEND") == 0) {
								int res =0;
								char *ipAddress = strtok(NULL," ");
								if(ipAddress == NULL) goto fail;
							//	printf("ip address entered : %s", ipAddress);
								char * clients_list_wk = malloc(strlen(clients_list)*sizeof(char));
								strcpy(clients_list_wk,clients_list);
								
								char *ips = strtok(clients_list_wk," ;");
								int found = 0;
								while(ips != NULL){
									if(strcmp(ips,ipAddress) == 0 ) found = 1;
									//printf("ips: %s\n",ips);
									ips = strtok(NULL," ;");
								} 
								if(found == 0) goto fail; 
								
								if((res = send(sockfd,input_bk,strlen(input_bk),0))== -1){
									perror("error login send");
									fflush(stdout);
									goto fail;
								}
								cse4589_print_and_log("[%s:SUCCESS]\n",command);		
								
							} else if(strcmp(command,"BROADCAST") == 0) {
								//TODO implement 
								int sent =0;
								if((sent = send(sockfd,input_bk,strlen(input_bk),0))== -1){
									perror("error login send");
									fflush(stdout);
									goto fail;	
								}
								//printf("bytes send = %i %s",sent,input_bk);
								cse4589_print_and_log("[%s:SUCCESS]\n",command);
								
							} else if(strcmp(command,"BLOCK") == 0) {
								//TODO implement 
								int res =0;
								char *ipAddress = strtok(NULL," ");
								if(ipAddress == NULL) goto fail;
								cse4589_print_and_log("ip address entered : %s", ipAddress);
								char * clients_list_wk = malloc(strlen(clients_list)*sizeof(char));
								strcpy(clients_list_wk,clients_list);
								
								char *ips = strtok(clients_list_wk," ;");
								int found = 0;
								while(ips != NULL){
									if(strcmp(ips,ipAddress) == 0 ) found = 1;
									//cse4589_print_and_log("ips: %s\n",ips);
									ips = strtok(NULL," ;");
								} 
								if(found == 0) goto fail; 
								
								if((res = send(sockfd,input_bk,strlen(input_bk),0))== -1){
									perror("error login send");
									fflush(stdout);
									goto fail;
								}
								cse4589_print_and_log("[%s:SUCCESS]\n",command);								
							} else if(strcmp(command,"UNBLOCK") == 0) {
								//TODO implement 
								int res =0;
								char *ipAddress = strtok(NULL," ");
								if(ipAddress == NULL) goto fail;
								cse4589_print_and_log("ip address entered : %s", ipAddress);
								char * clients_list_wk = malloc(strlen(clients_list)*sizeof(char));
								strcpy(clients_list_wk,clients_list);
								
								char *ips = strtok(clients_list_wk," ;");
								int found = 0;
								while(ips != NULL){
									if(strcmp(ips,ipAddress) == 0 ) found = 1;
								//	printf("ips: %s\n",ips);
									ips = strtok(NULL," ;");
								} 
								if(found == 0) goto fail; 
								
								if((res = send(sockfd,input_bk,strlen(input_bk),0))== -1){
									perror("error login send");
									fflush(stdout);
									goto fail;
								}
								cse4589_print_and_log("[%s:SUCCESS]\n",command);								
								
							} else if(strcmp(command,"SENDFILE") == 0) {
								//TODO implement 
							} else if(strcmp(command,"LOGOUT") == 0) {
								//TODO implement
								FD_CLR(sockfd,&master);
								close(sockfd);
								is_logged =0;
								cse4589_print_and_log("[%s:SUCCESS]\n",command);
							} else {
								cse4589_print_and_log("[%s:ERROR]\n", command);				
							}
				
							
						} else { fail:
								cse4589_print_and_log("[%s:ERROR]\n", command);				
						}					
						
					}
					else if(i == sockfd){
						command ="RECEIVED";
						int received;
						memset(&message, 0, sizeof(message));
						if((received = recv(sockfd,message,1000,0)) == -1){
						//	printf("error receiving client list"); 
						}
						if(received == 0){
						//	printf("server closed connection\n");
							close(i);
							FD_CLR(i,&master);
							is_logged = 0;
						}
						cse4589_print_and_log("[%s:SUCCESS]\n",command);
						cse4589_print_and_log("%s",message);
					}
					cse4589_print_and_log("[%s:END]\n", command);
					
				}
			}

			
			
		}

	} else {
		//entered something other than s or c in second argument
		cse4589_print_and_log("[%s:ERROR]\n", argv[0]);
		cse4589_print_and_log("[%s:END]\n", argv[0]);
		return 0;
	}
//	printf("reached end");
	end:
	return 0;
}

void sigchld_handler(int s){
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int get_eth0_ip(char * eth0_ip){
	struct ifaddrs *ifaddr, *ifa;
	int status = -1;
	if ((status = getifaddrs(&ifaddr)) == -1) {
		perror("getifaddrs");
		return status;
	}
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next){
		
		if (ifa->ifa_addr == NULL)
			continue;
		if(strcmp(ifa->ifa_name,"eth0") == 0 
		&& (ifa->ifa_addr->sa_family == AF_INET 
		|| ifa->ifa_addr->sa_family == AF_INET6 )){
			if((status = getnameinfo(ifa->ifa_addr,
					sizeof(struct sockaddr_in),
					eth0_ip,
					NI_MAXHOST,
					NULL,
					0,
					NI_NUMERICHOST)) != 0) {
					perror("getnameinfo");		
					}
			//printf("IP:%s\n",eth0_ip);
			break;
		}
		
	}
	freeifaddrs(ifaddr);
	return status;
}

int cl_list_add(struct sockaddr_storage their_addr,char* port, struct client_stat **start, struct client_stat **end){
	//printf("%lu %lu",sizeof(struct client_stat), sizeof(struct client_stat *)); 
	
	struct client_stat *tmp =  malloc(sizeof (struct client_stat));
	if (tmp == NULL) return -1;
	strcpy(tmp->cl_IP,inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr)) ;
	//printf("%s", tmp->cl_IP);
	//struct hostent *ent = gethostbyaddr(tmp->cl_IP,sizeof(tmp->cl_IP),((struct sockaddr_in*)&their_addr)->sin_family);
	//
	char host[40];

	if(getnameinfo((struct sockaddr*)&their_addr,sizeof(&their_addr),host,40,port,strlen(port),0) !=0){ perror("name error");};
//	printf("%s : %s",host, port);
	fflush(stdout);
	strcpy(tmp->cl_hostname, host);
	//sprintf(tmp->cl_port,"%s",port);
	tmp->cl_port = atoi(port);
	tmp->cl_loggedin = 1;
	tmp->cl_sent = 0;
	tmp->cl_received = 0;
	tmp->cl_blocked[0] = '\0';
	tmp->cl_next = NULL;	
	
	if(*start == NULL){
		*start = tmp;
		end = start;
	}else{
		(*end)->cl_next = tmp;
		*end = tmp;
	}
	
	
	return 0;
}
int cl_list_rem(struct sockaddr_storage *their_addr, struct client_stat **start, struct client_stat **end){
	struct client_stat *ptr = *start;
	struct client_stat *tmp;
	while(ptr->cl_next !=NULL){
		tmp = ptr;
		ptr = ptr->cl_next;
		if(strcmp(ptr->cl_IP,inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr)) == 0){
			tmp->cl_next = ptr->cl_next;
			free(ptr);
			return 0;
		}		
	}
	return -1;
}
int cl_list_upd(struct sockaddr_storage *their_addr, struct client_stat *start, struct client_stat *end, char param, int val){
	struct client_stat *ptr = start;
	while(ptr->cl_next !=NULL){
		if(strcmp(ptr->cl_IP,inet_ntoa(((struct sockaddr_in*)&their_addr)->sin_addr)) == 0){
			break;
		}
		if(ptr->cl_next == NULL)
			return -1;				
	}
	switch(param){
		case 'r': //received message
			ptr->cl_received += 1;
			break;
		case 's': //sent message
			ptr->cl_sent += 1;
		break;
		case 'i': //logged in
			ptr->cl_loggedin = 1;
			break;
		case 'o': //logged out
			ptr->cl_loggedin = 0;
			break;
		default :
			return -1;	
	}
	return -1;
}

int cl_list_srch(struct sockaddr_storage *their_addr, struct client_stat *start, struct client_stat *end, struct client_stat *client){
	//search by their_addr and set a client 
	return -1;
}

int cl_list_to_str(struct client_stat **start, char **list_str){
	//printf("inside tostr : %s",(*start)->cl_IP);
	struct client_stat *ptr = *start;
	int i = 1;
	while(ptr != NULL){
		char new_str[300];
		if(ptr->cl_loggedin == 0){ 
			ptr = ptr->cl_next;
			continue;
		}
		sprintf(new_str,"%d %s %s %i ;", i, ptr->cl_hostname, ptr->cl_IP, ptr->cl_port);
//	//	printf("%-5d%-35s%-20s%-8s;\n", i, ptr->cl_hostname, ptr->cl_IP, ptr->cl_port);
		i += 1;
		strcat(*list_str,new_str);							
		ptr = ptr->cl_next;
	}
	
	return 0;	
}

//int sort(struct client_stat **start){
//	struct client_stat *i;
//	
//	
//}
//int sort_list(struct client_stat **start){
//    struct client_stat * list_end = NULL;
//    while(list_end != start){  // this also takes care of the *start == NULL case, which you forgot!
//		struct client_stat *temp, *swap1;
//        swap1 = start;
//        while( swap1->cl_next != list_end )
//        {
//            if(swap1->cl_port > swap1->cl_next->cl_port)
//            {
//                struct client_stat *swap2 = swap1->cl_next;
//                swap1->cl_next = swap2->cl_next;
//                swap2->cl_next = swap1;
//                if(swap1 == start)
//                {
//                    start = swap2;
//                    swap1 = swap2;
//                }
//                else
//                {
//                    swap1 = swap2;
//                    temp->cl_next = swap2;
//                }
//            }
//            temp = swap1;
//            swap1 = swap1->cl_next;
//        }
//        // update the list_end to the last sorted element:
//        list_end = swap1;
//    }	
//	return 0;
//}
