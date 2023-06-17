#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include "csapp.h"
#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"


/*
 * Thread function for the thread that handles a particular client.
 *
 * @param  Pointer to a variable that holds the file descriptor for
 * the client connection.  This pointer must be freed once the file
 * descriptor has been retrieved.
 * @return  NULL
 *
 * This function executes a "service loop" that receives packets from
 * the client and dispatches to appropriate functions to carry out
 * the client's requests.  It also maintains information about whether
 * the client has logged in or not.  Until the client has logged in,
 * only LOGIN packets will be honored.  Once a client has logged in,
 * LOGIN packets will no longer be honored, but other packets will be.
 * The service loop ends when the network connection shuts down and
 * EOF is seen.  This could occur either as a result of the client
 * explicitly closing the connection, a timeout in the network causing
 * the connection to be closed, or the main thread of the server shutting
 * down the connection as part of graceful termination.
 */



void *jeux_client_service(void *arg){
	int signedIN = 0;
	int fd = *(int *)arg;
   	free(arg);
   	pthread_detach(pthread_self());
	struct sigaction sigpipe;
	sigpipe.sa_handler = SIG_IGN;
	sigpipe.sa_flags = 0;
	sigemptyset(&sigpipe.sa_mask);
	sigaction(SIGPIPE, &sigpipe, NULL);
   	CLIENT *c = creg_register(client_registry,fd);
   	JEUX_PACKET_HEADER *hdr =  calloc(1, sizeof(JEUX_PACKET_HEADER));
    void *payload = NULL;
    PLAYER *player = NULL;
    while (1) {
    	if (proto_recv_packet(fd, hdr, &payload) == 0){
	    	if (hdr -> type == JEUX_LOGIN_PKT){
	    		if (payload == NULL){
	    			client_send_nack(c);
	    		}
	    		char* username = (char*) payload;
	    		debug("%ld: name %s", pthread_self(), username);
	    		player = preg_register(player_registry, username);
				if (player == NULL){
					fprintf(stderr, "registering player error in jeux_client");
					client_send_nack(c);
				}
				if (client_login(c, player)){
					client_send_nack(c);
				}
				else{
					signedIN = 1;
					client_send_ack(c, NULL, 0);
				}
	    	}
	    	else if (signedIN){
	    		if (hdr -> type == JEUX_USERS_PKT){
	    			PLAYER ** players = creg_all_players(client_registry);
	    			int total_len = 0;
				    for (int i = 0; players[i] != NULL; i++) {
				        total_len += strlen(player_get_name(players[i]));
				        total_len += strlen("\t");
				        int rating = player_get_rating(players[i]);
						char rating_str[15];
						snprintf(rating_str, sizeof(rating_str), "%d", rating);
				        total_len += strlen(rating_str);
				        total_len += strlen("\n");
				    }

				    // Allocate memory for the concatenated username string
				    char *usernames = calloc(total_len + 1, sizeof(char));
				    if (usernames == NULL) {
				        client_send_nack(c);
				    }
				    for (int i = 0; players[i] != NULL; i++) {
					    strcat(usernames, player_get_name(players[i]));
					    strcat(usernames, "\t");
					    int rating = player_get_rating(players[i]);
					    char rating_str[15];
					    snprintf(rating_str, sizeof(rating_str), "%d", rating);
					    strcat(usernames, rating_str);
					    strcat(usernames, "\n");

					}
					client_send_ack(c, usernames, total_len);

	    		}
	    		else if (hdr -> type == JEUX_INVITE_PKT){
	    			int sRole;
	    			if (hdr -> role == 1){
	    				sRole = 2;
	    			}
	    			else{
	    				sRole = 1;
	    			}
	    			uint16_t datasize  = ntohs(hdr->size);
		    		char* username = (char*) payload;
		    		username[datasize] = '\0';
		    		CLIENT *targetC = creg_lookup(client_registry,username);
		    		if (targetC == NULL){
		    			debug("%ld: fail invite", pthread_self());
		    			client_send_nack(c);
		    		}
		    		else{
		    			debug("%ld: targetfound", pthread_self());
		    			int id = client_make_invitation(c, targetC, sRole, hdr -> role);
		    			if (id == -1){
		    				client_send_nack(c);
		    			}
		    			else{
		    				hdr -> type = JEUX_ACK_PKT;
		    				hdr -> id = id;
							hdr -> size = 0;
							client_send_packet(c, hdr, NULL);
		    			}

		    		}
		    	}
		    	else if (hdr -> type == JEUX_REVOKE_PKT){
		    		if (client_revoke_invitation(c, hdr -> id)){
		    			client_send_nack(c);
		    		}
		    		else{
						client_send_ack(c, NULL, 0);
		    		}
		    	}
		    	else if (hdr -> type == JEUX_ACCEPT_PKT){
		    		char * gamestate = NULL;
		    		if (client_accept_invitation(c, hdr->id, &gamestate)){
		    			client_send_nack(c);
		    		}
		    		else{
		    			if (gamestate == NULL){
		    				debug("%ld: sending ack game state NULL", pthread_self());
		    				client_send_ack(c, NULL, 0);

		    			}
		    			else{
		    				debug("%ld: sending ack game state %s", pthread_self(), gamestate);
		    				client_send_ack(c, gamestate, 18);
		    			}
		    		}
		    	}
		    	else if (hdr -> type == JEUX_DECLINE_PKT){
		    		if (client_decline_invitation(c, hdr -> id)){
		    			client_send_nack(c);
		    		}
		    		else{
						client_send_ack(c, NULL, 0);
		    		}
		    	}
		    	else if (hdr -> type == JEUX_MOVE_PKT){
		    		debug("%ld: got moved %s", pthread_self(), (char *)payload);
		    		uint16_t datasize  = ntohs(hdr->size);
	    			char *payloadstr = realloc(payload, datasize + 1);
	    			payloadstr[datasize] = '\0';
	    			payload = payloadstr;
		    		if (client_make_move(c, hdr -> id,payloadstr)){
		    			client_send_nack(c);
		    		}

		    	}
		    	else if (hdr -> type == JEUX_RESIGN_PKT){
		    		if (client_resign_game(c, hdr -> id)){
		    			client_send_nack(c);
		    		}
		    		else{
		    			client_send_ack(c, NULL, 0);
		    		}
		    	}
	    		else{
	    			client_send_nack(c);
	    		}
	    	}
	    	else{
	    		client_send_nack(c);
	    	}
	    	if (payload != NULL){
	    		free(payload);
	    	}
	    }
	    else{
	    	client_logout(c);
	    	if (player != NULL){
				player_unref(player, "logging out player");
	    	}
			creg_unregister(client_registry, c);
			free(hdr);
			pthread_exit(NULL);

	    }
	}

}