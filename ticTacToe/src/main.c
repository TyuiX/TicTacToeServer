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

#ifdef DEBUG
int _debug_packets_ = 1;
#endif
int debug = 0;
int exitFlag = 1;

typedef struct sockaddr SA;

static void terminate(int status);

void sighup_handler(int signum){
    if (signum == SIGHUP){
        exitFlag = 0;
        terminate(EXIT_SUCCESS);
    }
}

/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    char* port = NULL;
    //char *host = "localhost";
    int opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = optarg;
                if (port == NULL || strcmp(optarg, "0") == 0) {
                    fprintf(stderr, "Error: invalid port number \n");
                    exit(1);
                }
                break;
            default:
                fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
                exit(1);
        }
    }
    if (port == NULL){
        fprintf(stderr, "Error: port number must be specified\n");
        exit(EXIT_FAILURE);
    }

    // on which the server should listen.
    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();
    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function jeux_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    listenfd = Open_listenfd(port);
    struct sigaction signUpAction;
    sigemptyset(&signUpAction.sa_mask);
    signUpAction.sa_flags = SA_RESTART;
    signUpAction.sa_handler = sighup_handler;
    sigaction(SIGHUP, &signUpAction, NULL);

    while (exitFlag){
        debug("%ld: making an connection..", pthread_self());
        clientlen = sizeof(struct sockaddr_storage);
        connfd = malloc(sizeof(int));
        if (connfd != NULL){
            *connfd = accept(listenfd, (SA*)&clientaddr, &clientlen);
            pthread_t tid;
            pthread_create(&tid, NULL, jeux_client_service, connfd);
        }
    }

    fprintf(stderr, "You have to finish implementing main() "
	    "before the Jeux server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);
    
    debug("%ld: Waiting for service threads to terminate...", pthread_self());
    creg_wait_for_empty(client_registry);
    debug("%ld: All service threads terminated.", pthread_self());

    // Finalize modules.
    creg_fini(client_registry);
    preg_fini(player_registry);

    debug("%ld: Jeux server terminating", pthread_self());
    exit(status);
}
