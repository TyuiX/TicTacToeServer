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
#include <semaphore.h>

#include "debug.h"
#include "protocol.h"
#include "client_registry.h"
#include "client.h"
#include "player.h"

/*
 * The CLIENT_REGISTRY type is a structure that defines the state of a
 * client registry.  You will have to give a complete structure
 * definition in client_registry.c.  The precise contents are up to
 * you.  Be sure that all the operations that might be called
 * concurrently are thread-safe.
 */

#include "client.h"

/*
 * Initialize a new client registry.
 *
 * @return  the newly initialized client registry, or NULL if initialization
 * fails.
 */
typedef struct client_registry{
    int clientsAmount;
    CLIENT *clients[MAX_CLIENTS];
    pthread_mutex_t mutex;
    sem_t semaphore;

} CLIENT_REGISTRY;
CLIENT_REGISTRY *creg_init(){
	CLIENT_REGISTRY *clientReg = malloc(sizeof(CLIENT_REGISTRY));
    if (clientReg == NULL) {
        return NULL;
    }
    clientReg -> clientsAmount = 0;
    if (pthread_mutex_init(& clientReg ->mutex, NULL) != 0) {
        free(clientReg);
        return NULL;
    }
    if (sem_init(&clientReg ->semaphore, 0, 1) != 0) {
        pthread_mutex_destroy(&clientReg ->mutex);
        free(clientReg);
        return NULL;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientReg->clients[i] = NULL;
    }
    return clientReg;
}

void creg_fini(CLIENT_REGISTRY *cr){    
    pthread_mutex_destroy(&cr ->mutex);
	free(cr);
}
/*
 * Register a client file descriptor.
 * If successful, returns a reference to the the newly registered CLIENT,
 * otherwise NULL.  The returned CLIENT has a reference count of one.
 *
 * @param cr  The client registry.
 * @param fd  The file descriptor to be registered.
 * @return a reference to the newly registered CLIENT, if registration
 * is successful, otherwise NULL.
 */
CLIENT *creg_register(CLIENT_REGISTRY *cr, int fd){
    if (cr == NULL){
        return NULL;
    }
	pthread_mutex_lock(&cr->mutex);
	CLIENT *c = client_create(cr, fd);
    if (c == NULL){
         debug("%ld: error when creating client", pthread_self());
        return NULL;
    }
	for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr ->clients[i] == NULL){
        	cr -> clients[i] = c;
            break;
        }
    }
    cr -> clientsAmount += 1;
    if (cr -> clientsAmount == 1){
        debug("%ld: decreasing semaphore", pthread_self());
        sem_wait(&cr->semaphore);
    }
    pthread_mutex_unlock(&cr->mutex);
	return c;
}
/*
 * Unregister a CLIENT, removing it from the registry.
 * The client reference count is decreased by one to account for the
 * pointer discarded by the client registry.  If the number of registered
 * clients is now zero, then any threads that are blocked in
 * creg_wait_for_empty() waiting for this situation to occur are allowed
 * to proceed.  It is an error if the CLIENT is not currently registered
 * when this function is called.
 *
 * @param cr  The client registry.
 * @param client  The CLIENT to be unregistered.
 * @return 0  if unregistration succeeds, otherwise -1.
 */
int creg_unregister(CLIENT_REGISTRY *cr, CLIENT *client){
    if (cr == NULL){
        return -1;
    }
    pthread_mutex_lock(&cr->mutex);
    if (client == NULL){
        debug("%ld: Null Client", pthread_self());

        pthread_mutex_unlock(&cr->mutex);
        return -1;
    }
    debug("%ld: unregister", pthread_self());
	for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr -> clients[i] == client){
            int fd = client_get_fd(client);
            close(fd);
        	cr -> clientsAmount -= 1;
        	client_unref(cr -> clients[i], "removing client from registry");
        	cr -> clients[i] = NULL;
        	if (cr -> clientsAmount == 0){
                debug("%ld: increasing semaphore to 1", pthread_self());
        		sem_post(&cr->semaphore);
        	}
        	pthread_mutex_unlock(&cr->mutex);
        	return 0;

        }
    }
    pthread_mutex_unlock(&cr->mutex);
    return -1;
}

/*
 * Given a username, return the CLIENT that is logged in under that
 * username.  The reference count of the returned CLIENT is
 * incremented by one to account for the reference returned.
 *
 * @param cr  The registry in which the lookup is to be performed.
 * @param user  The username that is to be looked up.
 * @return the CLIENT currently registered under the specified
 * username, if there is one, otherwise NULL.
 */
CLIENT *creg_lookup(CLIENT_REGISTRY *cr, char *user){
    if (cr == NULL){
        return NULL;
    }
	pthread_mutex_lock(&cr->mutex);
	for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr ->clients[i] != NULL){
        	PLAYER *p = client_get_player(cr -> clients[i]);
        	if (p != NULL){
                debug("%ld: playername: %s, name: %s", pthread_self(), player_get_name(p), user);
        		if (strcmp(player_get_name(p), user) == 0){
        			pthread_mutex_unlock(&cr->mutex);
        			return client_ref(cr -> clients[i] , "creg_lookup username");
        		}
        	}

        }
    }
    pthread_mutex_unlock(&cr->mutex);
	return NULL;
}

/*
 * Return a list of all currently logged in players.  The result is
 * returned as a malloc'ed array of PLAYER pointers, with a NULL
 * pointer marking the end of the array.  It is the caller's
 * responsibility to decrement the reference count of each of the
 * entries and to free the array when it is no longer needed.
 *
 * @param cr  The registry for which the set of usernames is to be
 * obtained.
 * @return the list of players as a NULL-terminated array of pointers.
 */
PLAYER **creg_all_players(CLIENT_REGISTRY *cr){
    if (cr == NULL) {
        return NULL;
    }
	pthread_mutex_lock(&cr->mutex);
    PLAYER **players =malloc(sizeof(PLAYER*)*MAX_CLIENTS);
    int num_players = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
    	if (cr ->clients[i] != NULL){
            PLAYER *p = client_get_player(cr -> clients[i]);
    		if (p != NULL){
    			players[num_players] = p;
    			num_players ++;

    		}

        }
    }
    for (int i = num_players; i < MAX_CLIENTS; i++){
        players[i] = NULL;
    }
    pthread_mutex_unlock(&cr->mutex);
    return players;
}


/*
 * A thread calling this function will block in the call until
 * the number of registered clients has reached zero, at which
 * point the function will return.  Note that this function may be
 * called concurrently by an arbitrary number of threads.
 *
 * @param cr  The client registry.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr){
    debug("%ld: Waiting", pthread_self());
    sem_wait(&cr->semaphore);
    debug("%ld: wait complete", pthread_self());

}
/*
 * Shut down (using shutdown(2)) all the sockets for connections
 * to currently registered clients.  The clients are not unregistered
 * by this function.  It is intended that the clients will be
 * unregistered by the threads servicing their connections, once
 * those server threads have recognized the EOF on the connection
 * that has resulted from the socket shutdown.
 *
 * @param cr  The client registry.
 */
void creg_shutdown_all(CLIENT_REGISTRY *cr){
    debug("%ld: shutting down all", pthread_self());
    pthread_mutex_lock(&cr->mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        CLIENT *client = cr->clients[i];
        if (client != NULL) {
            int fd = client_get_fd(client);
            shutdown(fd, SHUT_RD);
        }
    }
    pthread_mutex_unlock(&cr->mutex);
    
}