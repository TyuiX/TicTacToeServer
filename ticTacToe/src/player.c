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
#include <math.h>
#include "debug.h"
#include "protocol.h"
#include "client_registry.h"
#include "client.h"
#include "player.h"
#include "invitation.h"
#include "jeux_globals.h"

/*
 * A PLAYER represents a user of the system.  A player has a username,
 * which does not change, and also has a "rating", which is a value
 * that reflects the player's skill level among all players known to
 * the system.  The player's rating changes as a result of each game
 * in which the player participates.  PLAYER objects are managed by
 * the player registry.  So that a PLAYER object can be passed around
 * externally to the player registry without fear of dangling
 * references, it has a reference count that corresponds to the number
 * of references that exist to the object.  A PLAYER object will not
 * be freed until its reference count reaches zero.
 */

/*
 * The PLAYER type is a structure type that defines the state of a player.
 * You will have to give a complete structure definition in player.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
typedef struct player {
	char* username;
	int rating;
	int reference;
    long int nameLength;
    sem_t seph;
} PLAYER;

/*
 * Create a new PLAYER with a specified username.  A private copy is
 * made of the username that is passed.  The newly created PLAYER has
 * a reference count of one, corresponding to the reference that is
 * returned from this function.
 *
 * @param name  The username of the PLAYER.
 * @return  A reference to the newly created PLAYER, if initialization
 * was successful, otherwise NULL.
 */
PLAYER *player_create(char *name){
	PLAYER *p = malloc(sizeof(PLAYER));
    p->username = calloc(strlen(name) + 1, sizeof(char)); // allocate memory for username
    strcpy(p->username, name);
	p -> rating = PLAYER_INITIAL_RATING;
	p -> reference = 1;
    sem_init(&p->seph, 0, 1);
	return p;

}

/*
 * Increase the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same PLAYER object that was passed as a parameter.
 */
PLAYER *player_ref(PLAYER *player, char *why){
    if (player == NULL){
        return NULL;
    }
    sem_wait(&player->seph);
	debug("%ld: %p %s", pthread_self(), player, why);
	player -> reference += 1;
    sem_post(&player->seph);
	return player;
}
void player_unref(PLAYER *player, char *why){
    if (player != NULL){
        sem_wait(&player->seph);
    	debug("%ld: %p %s", pthread_self(), player, why);
    	player -> reference -= 1;
    	if (player -> reference == 0){
            sem_post(&player->seph);
            free(player->username);
    		free(player);
    	}
        else{
            sem_post(&player->seph);
        }
    }
}



/*
 * Get the username of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the username of the player.
 */
char *player_get_name(PLAYER *player){
	if (player == NULL){
		return NULL;
	}
	return player->username;
}

/*
 * Get the rating of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the rating of the player.
 */
int player_get_rating(PLAYER *player){
	if (player == NULL){
		return -1;
	}
	return player->rating;
}

/*
 * Post the result of a game between two players.
 * To update ratings, we use a system of a type devised by Arpad Elo,
 * similar to that used by the US Chess Federation.
 * The player's ratings are updated as follows:
 * Assign each player a score of 0, 0.5, or 1, according to whether that
 * player lost, drew, or won the game.
 * Let S1 and S2 be the scores achieved by player1 and player2, respectively.
 * Let R1 and R2 be the current ratings of player1 and player2, respectively.
 * Let E1 = 1/(1 + 10**((R2-R1)/400)), and
 *     E2 = 1/(1 + 10**((R1-R2)/400))
 * Update the players ratings to R1' and R2' using the formula:
 *     R1' = R1 + 32*(S1-E1)
 *     R2' = R2 + 32*(S2-E2)
 *
 * @param player1  One of the PLAYERs that is to be updated.
 * @param player2  The other PLAYER that is to be updated.
 * @param result   0 if draw, 1 if player1 won, 2 if player2 won.
 */
void player_post_result(PLAYER *player1, PLAYER *player2, int result){
    if (player1 != NULL && player2 != NULL && result <= 2){
        double s1, s2, e1, e2;
        int r1, r2;
        if (result == 0){
            s1 = 0.5;
            s2 = 0.5;
        }
        else if (result == 1){
            s1 = 1;
            s2 = 0;
        }
        else{
            s1 = 0;
            s2 = 1;
        }
        r1 = player_get_rating(player1);
        r2 = player_get_rating(player2);
        e1 = 1/(1+ pow(10,((r2-r1)/400)));
        e2 = 1/(1+ pow(10,((r1-r2)/400)));
        sem_wait(&player1->seph);
        player1 -> rating = r1 + (int)(32*(s1 - e1));
        sem_post(&player1->seph);
        sem_wait(&player2->seph);
        player2 -> rating = r2 + (int)(32*(s2 - e2));
        sem_post(&player2->seph);
    }

}