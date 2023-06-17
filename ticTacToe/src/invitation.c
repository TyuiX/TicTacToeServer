
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
/*
 * Create an INVITATION in the OPEN state, containing reference to
 * specified source and target CLIENTs, which cannot be the same CLIENT.
 * The reference counts of the source and target are incremented to reflect
 * the stored references.
 *
 * @param source  The CLIENT that is the source of this INVITATION.
 * @param target  The CLIENT that is the target of this INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of this INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of this INVITATION.
 * @return a reference to the newly created INVITATION, if initialization
 * was successful, otherwise NULL.
 */
long int GlobalId = 0;
typedef struct invitation {
	int ref;
	CLIENT * source;
	CLIENT * target;
	INVITATION_STATE state;
	GAME_ROLE sourceR;
	GAME_ROLE targetR;
	GAME *gameRef;
	sem_t seph;
} INVITATION;
INVITATION *inv_create(CLIENT *source, CLIENT *target,
	GAME_ROLE source_role, GAME_ROLE target_role){
	if  (source == target){
		return NULL;
	}
	client_ref(source, "reference by invitation creation as source");
	client_ref(target, "reference by invitation creation as target");
	INVITATION *inv = malloc(sizeof(INVITATION));
	inv -> ref = 1;
	inv -> source = source;
	inv -> target = target;
	inv-> sourceR = source_role;
	inv -> targetR = target_role;
	inv -> gameRef = NULL;
	inv -> state = INV_OPEN_STATE;
	sem_init(&inv->seph, 0, 1);
	return inv;

}
/*
 * Increase the reference count on an invitation by one.
 *
 * @param inv  The INVITATION whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same INVITATION object that was passed as a parameter.
 */
INVITATION *inv_ref(INVITATION *inv, char *why){
	if (inv == NULL){
		return NULL;
	}
	sem_wait(&inv->seph);
	inv -> ref += 1;
	debug("%ld: %p %s", pthread_self(), inv, why);
	sem_post(&inv->seph);
	return inv;
}

/*
 * Decrease the reference count on an invitation by one.
 * If after decrementing, the reference count has reached zero, then the
 * invitation and its contents are freed.
 *
 * @param inv  The INVITATION whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void inv_unref(INVITATION *inv, char *why){
	sem_wait(&inv->seph);
	if (inv != NULL){
		inv -> ref -= 1;
		debug("%ld: %p %s", pthread_self(), inv, why);
		if (inv -> ref == 0){
			client_unref(inv->source, "freeing invitation");
			client_unref(inv->target, "freeing invitation");
			sem_post(&inv->seph);
			free(inv);
		}
		else{
			sem_post(&inv->seph);
		}
	}
}

/*
 * Get the CLIENT that is the source of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid as
 * long as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the source of the INVITATION.
 */
CLIENT *inv_get_source(INVITATION *inv){
	if (inv == NULL){
		return NULL;
	}
	return inv -> source;
}

/*
 * Get the CLIENT that is the target of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid if
 * the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the target of the INVITATION.
 */
CLIENT *inv_get_target(INVITATION *inv){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);
		return NULL;
	}
	sem_post(&inv->seph);
	return inv -> target;
}
/*
 * Get the GAME_ROLE to be played by the source of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the source of the INVITATION.
 */
GAME_ROLE inv_get_source_role(INVITATION *inv){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);

		return -1;
	}
	sem_post(&inv->seph);

	return inv -> sourceR;
}

/*
 * Get the GAME_ROLE to be played by the target of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the target of the INVITATION.
 */
GAME_ROLE inv_get_target_role(INVITATION *inv){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);
		return -1;
	}
	sem_post(&inv->seph);
	return inv -> targetR;
}

/*
 * Get the GAME (if any) associated with an INVITATION.
 * The reference count of the returned GAME is NOT incremented,
 * so the GAME reference should only be regarded as valid as long
 * as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME associated with the INVITATION, if there is one,
 * otherwise NULL.
 */
GAME *inv_get_game(INVITATION *inv){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);
		return NULL;
	}
	sem_post(&inv->seph);
	return inv-> gameRef;
}

/*
 * Accept an INVITATION, changing it from the OPEN to the
 * ACCEPTED state, and creating a new GAME.  If the INVITATION was
 * not previously in the the OPEN state then it is an error.
 *
 * @param inv  The INVITATION to be accepted.
 * @return 0 if the INVITATION was successfully accepted, otherwise -1.
 */
int inv_accept(INVITATION *inv){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);
		return -1;
	}
	if (inv -> state == INV_CLOSED_STATE || inv-> state == INV_ACCEPTED_STATE){
		sem_post(&inv->seph);
		return -1;
	}
	inv -> state = INV_ACCEPTED_STATE;
	inv -> gameRef = game_create();
	sem_post(&inv->seph);
	return 0;
}

/*
 * Close an INVITATION, changing it from either the OPEN state or the
 * ACCEPTED state to the CLOSED state.  If the INVITATION was not previously
 * in either the OPEN state or the ACCEPTED state, then it is an error.
 * If INVITATION that has a GAME in progress is closed, then the GAME
 * will be resigned by a specified player.
 *
 * @param inv  The INVITATION to be closed.
 * @param role  This parameter identifies the GAME_ROLE of the player that
 * should resign as a result of closing an INVITATION that has a game in
 * progress.  If NULL_ROLE is passed, then the invitation can only be
 * closed if there is no game in progress.
 * @return 0 if the INVITATION was successfully closed, otherwise -1.
 */
int inv_close(INVITATION *inv, GAME_ROLE role){
	sem_wait(&inv->seph);
	if (inv == NULL){
		sem_post(&inv->seph);
		return -1;
	}
	if (inv -> state == INV_CLOSED_STATE){
		sem_post(&inv->seph);
		return -1;
	}
	if (role != NULL_ROLE){
		game_resign(inv -> gameRef, role);

	}
	else{
		if (inv -> gameRef != NULL){
			sem_post(&inv->seph);
			return -1;
		}
	}
	inv -> state = INV_CLOSED_STATE;
	sem_post(&inv->seph);
	return 0;
}