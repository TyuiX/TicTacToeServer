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
#include "invitation.h"
#include "jeux_globals.h"
#include "player_registry.h"

/*
 * A player registry maintains a mapping from usernames to PLAYER objects.
 * Entries persist for as long as the server is running.
 */

/*
 * The PLAYER_REGISTRY type is a structure type that defines the state
 * of a player registry.  You will have to give a complete structure
 * definition in player_registry.c. The precise contents are up to
 * you.  Be sure that all the operations that might be called
 * concurrently are thread-safe.
 */
typedef struct player_registry {
	int playersAmount;
    PLAYER *players[1000];
    sem_t seph;

} PLAYER_REGISTRY;

/*
 * Initialize a new player registry.
 *
 * @return the newly initialized PLAYER_REGISTRY, or NULL if initialization
 * fails.
 */
PLAYER_REGISTRY *preg_init(void){
	PLAYER_REGISTRY * pr = malloc(sizeof(PLAYER_REGISTRY));
	if (pr == NULL){
		return NULL;
	}
    for (int i = 0; i < 1000; i++){
        pr -> players[i] = NULL;
    }
	sem_init(&pr->seph, 0, 1);
	return pr;
}

/*
 * Finalize a player registry, freeing all associated resources.
 *
 * @param cr  The PLAYER_REGISTRY to be finalized, which must not
 * be referenced again.
 */
void preg_fini(PLAYER_REGISTRY *preg){
	free(preg);
}

/*
 * Register a player with a specified user name.  If there is already
 * a player registered under that user name, then the existing registered
 * player is returned, otherwise a new player is created.
 * If an existing player is returned, then its reference count is increased
 * by one to account for the returned pointer.  If a new player is
 * created, then the returned player has reference count equal to two:
 * one count for the pointer retained by the registry and one count for
 * the pointer returned to the caller.
 *
 * @param name  The player's user name, which is copied by this function.
 * @return A pointer to a PLAYER object, in case of success, otherwise NULL.
 *
 */
PLAYER *preg_register(PLAYER_REGISTRY *preg, char *name){
	sem_wait(&preg->seph);
	for (int i = 0; i < 1000; i++){
		if (preg -> players[i] == NULL){
			PLAYER *p = player_create(name);
            if (p == NULL){
                debug("%ld: fail create player", pthread_self());
                sem_post(&preg->seph);
                return p;
            }
			player_ref(p, "Being used by register and an client");
            preg -> players[i] = p;
			sem_post(&preg->seph);
            debug("%ld: found slot", pthread_self());
			return p;
		}
		else{
            
			if (strcmp(player_get_name(preg -> players[i]), name) == 0){
				player_ref(preg -> players[i], "Being used by register and an client");
				sem_post(&preg->seph);
				return preg -> players[i];
			}
		}
	}
	sem_post(&preg->seph);
	return NULL;
}