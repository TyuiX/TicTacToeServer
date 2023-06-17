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
#include <time.h>
#include <stdint.h>

#include "debug.h"
#include "protocol.h"
#include "client_registry.h"
#include "client.h"
#include "player.h"
#include "invitation.h"
#include "jeux_globals.h"
#include "game.h"

typedef struct game {
	int ref;
	int gameboard[3][3];
	sem_t seph;
	GAME_ROLE expectedTurn;
	char player1Sym;
	char player2Sym;
	int gameover;
	GAME_ROLE winner;
} GAME;

/*
 * The GAME_MOVE type is a structure type that defines a move in a game.
 * The details are up to you.  A GAME_MOVE is immutable.
 */
typedef struct game_move{
	int cord;
	GAME_ROLE turn;
	char sym;
} GAME_MOVE;

/*
 * A GAME represents the current state of a game between participating
 * players.  So that a GAME object can be passed around 
 * without fear of dangling references, it has a reference count that
 * corresponds to the number of references that exist to the object.
 * A GAME object will not be freed until its reference count reaches zero.
 */

/*
 * The GAME type is a structure type that defines the state of a game.
 * You will have to give a complete structure definition in game.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
GAME *game_create(){
	GAME * g = malloc(sizeof(GAME));
	g -> ref = 1;
	sem_init(&g->seph, 0, 1);
	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 3; j++){
			g -> gameboard[i][j] = 0;
		}
	}
	g -> expectedTurn = FIRST_PLAYER_ROLE;
	g-> gameover = 0;
	g -> winner = NULL_ROLE;
	g -> player1Sym = ' ';
	g-> player2Sym = ' ';
	return g;
}
GAME *game_ref(GAME *game, char *why){
	sem_wait(&game -> seph);
	debug("%ld: %p %s", pthread_self(), game, why);
	game -> ref += 1;
	sem_post(&game-> seph);
	return game;
}
/*
 * Decrease the reference count on a game by one.  If after
 * decrementing, the reference count has reached zero, then the
 * GAME and its contents are freed.
 *
 * @param game  The GAME whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void game_unref(GAME *game, char *why){
	sem_wait(&game -> seph);
	debug("%ld: %p %s", pthread_self(), game, why);
	game -> ref -= 1;
	if (game -> ref == 0){
		sem_post(&game-> seph);
		free(game);
	}
	else{
		sem_post(&game-> seph);
	}

}
/*
 * Apply a GAME_MOVE to a GAME.
 * If the move is illegal in the current GAME state, then it is an error.
 *
 * @param game  The GAME to which the move is to be applied.
 * @param move  The GAME_MOVE to be applied to the game.
 * @return 0 if application of the move was successful, otherwise -1.
 */
int game_apply_move(GAME *game, GAME_MOVE *move){

	if (game == NULL || move == NULL){
		return -1;
	}
	sem_wait(&game -> seph);

	int cord = move -> cord;
	char turn = move -> turn;
	if (turn != game -> expectedTurn){
		sem_post(&game-> seph);
		return -1;
	}
	if (turn == FIRST_PLAYER_ROLE){
		if (game -> player1Sym != move-> sym){
			sem_post(&game-> seph);
			return -1;
		}
	}
	else{
		if (game -> player2Sym != move-> sym){
			sem_post(&game-> seph);
			return -1;
		}
	}
	int counter = 1;
	int found = 0;
	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 3; j++){
			if (cord == counter){
				if (game -> gameboard[i][j] != 0){
					return -1;
				}
				game -> gameboard[i][j] = turn;
				if (game -> expectedTurn == 1){
					game -> expectedTurn = SECOND_PLAYER_ROLE;
				}
				else{
					game -> expectedTurn = FIRST_PLAYER_ROLE;
				}
				found = 1;
				break;
			}
			counter += 1;
		}
		if (found){
			break;
		}
	}
	if (found == 0){
		return -1;
	}
	// checker for winner 
	//check if the row are the same
	int gameEnded = 0;
	for (int i = 0; i < 3; i ++){
		if (game->gameboard[i][0] == game->gameboard[i][1] &&  game->gameboard[i][0] == game->gameboard[i][2]){
			if (game->gameboard[i][0] != 0){
				game -> gameover = 1;
				game -> winner = game->gameboard[i][0];
				debug("%ld: row", pthread_self());
				gameEnded = 1;
				break;
			} 
		}
	}
	//check for col
	if (gameEnded == 0){
		for (int i = 0; i < 3; i ++){
			if (game->gameboard[0][i] == game->gameboard[1][i] &&  game->gameboard[0][i] == game->gameboard[2][i]){
				if (game->gameboard[0][i] != 0){
					game -> gameover = 1;
					game -> winner = game->gameboard[i][0];
					debug("%ld: col", pthread_self());
					gameEnded = 1;
					break;
				} 
			}
		}
	}
	//check for diangle
	if (gameEnded == 0){
		if (game->gameboard[0][0] == game->gameboard[1][1] &&  game->gameboard[0][0] == game->gameboard[2][2]){
			if (game->gameboard[0][0] != 0){
				game -> gameover = 1;
				game -> winner = game->gameboard[0][0];
				debug("%ld: left angle", pthread_self());
				gameEnded = 1;
			} 
		}
	}
	if (gameEnded == 0){
		if (game->gameboard[2][0] == game->gameboard[1][1] &&  game->gameboard[2][0] == game->gameboard[0][2]){
			if (game->gameboard[2][0] != 0){
				game -> gameover = 1;
				game -> winner = game->gameboard[0][0];
				debug("%ld: right angle", pthread_self());
				gameEnded = 1;
			} 
		}
	}
	if (gameEnded){
		debug("%ld: game ended after move", pthread_self());
		debug("%ld: winner %d", pthread_self(), game -> winner);
	}
	sem_post(&game-> seph);
	return 0;

}
/*
 * Submit the resignation of the GAME by the player in a specified
 * GAME_ROLE.  It is an error if the game has already terminated.
 *
 * @param game  The GAME to be resigned.
 * @param role  The GAME_ROLE of the player making the resignation.
 * @return 0 if resignation was successful, otherwise -1.
 */
int game_resign(GAME *game, GAME_ROLE role){
	if (game == NULL){
		return -1;
	}
	sem_wait(&game -> seph);
	if (role == NULL_ROLE){
		sem_post(&game-> seph);
		return -1;
	}
	if (role == FIRST_PLAYER_ROLE){
		game -> winner = SECOND_PLAYER_ROLE;
	}
	else{
		game -> winner = SECOND_PLAYER_ROLE;
	}
	game -> gameover = 1;
	sem_post(&game-> seph);
	return 0;

}

/*
 * Get a string that describes the current GAME state, in a format
 * appropriate for human users.  The returned string is in malloc'ed
 * storage, which the caller is responsible for freeing when the string
 * is no longer required.
 *
 * @param game  The GAME for which the state description is to be
 * obtained.
 * @return  A string that describes the current GAME state.
 */
char *game_unparse_state(GAME *game){
	debug("%ld: getting gamestate", pthread_self());
	sem_wait(&game -> seph);
	char* gamestate = calloc(1, sizeof(char)*19);
	char * pointer = gamestate;
	for (int i = 0; i < 3; i++){
		for (int j = 0; j < 3; j++){
			if (game -> gameboard[i][j] == 0){
				*pointer = ' ';
				 pointer += 1;

			}
			else if (game -> gameboard[i][j] == 1){
				*pointer = game -> player1Sym;
				pointer += 1;
			}
			else{
				*pointer = game -> player2Sym;
				pointer += 1;
			}
			if (j != 2){
				*pointer = '|';
				pointer += 1;
			}
		}
		*pointer = '\n';
		pointer += 1;
	}
	pointer = '\0';
	sem_post(&game-> seph);
	return gamestate;
}

/*
 * Determine if a specifed GAME has terminated.
 *
 * @param game  The GAME to be queried.
 * @return 1 if the game is over, 0 otherwise.
 */
int game_is_over(GAME *game){
	if (game == NULL){
		return 0;
	}
	if (game -> gameover){
		return 1;
	}
	return 0;
}

/*
 * Get the GAME_ROLE of the player who has won the game.
 *
 * @param game  The GAME for which the winner is to be obtained.
 * @return  The GAME_ROLE of the winning player, if there is one.
 * If the game is not over, or there is no winner because the game
 * is drawn, then NULL_PLAYER is returned.
 */
GAME_ROLE game_get_winner(GAME *game){
	if (game == NULL){
		return NULL_ROLE;
	}
	return game -> winner;
}

/*
 * Attempt to interpret a string as a move in the specified GAME.
 * If successful, a GAME_MOVE object representing the move is returned,
 * otherwise NULL is returned.  The caller is responsible for freeing
 * the returned GAME_MOVE when it is no longer needed.
 * Refer to the assignment handout for the syntax that should be used
 * to specify a move.
 *
 * @param game  The GAME for which the move is to be parsed.
 * @param role  The GAME_ROLE of the player making the move.
 * If this is not NULL_ROLE, then it must agree with the role that is
 * currently on the move in the game.
 * @param str  The string that is to be interpreted as a move.
 * @return  A GAME_MOVE described by the given string, if the string can
 * in fact be interpreted as a move, otherwise NULL.
 */
GAME_MOVE *game_parse_move(GAME *game, GAME_ROLE role, char *str){
	if (role == NULL_ROLE){
		return NULL;
	}
	if (game == NULL){
		return NULL;
	}
	sem_wait(&game -> seph);
	GAME_MOVE *move = malloc(sizeof(GAME_MOVE));
	debug("%ld: paring move  %s", pthread_self(), str);
	int length = strlen(str);
	debug("%ld: move  length %d", pthread_self(), length);
	if (length == 1){
		move -> cord = str[0] - '0';
		if (role == FIRST_PLAYER_ROLE){
			if (game -> player1Sym == ' '){
				if (game -> player2Sym == 'X'){
					game -> player1Sym = 'O';

				}
				else{
					game -> player1Sym = 'X';
				}

			}
			move -> sym = game -> player1Sym;

		}
		else{
			if (game -> player2Sym == ' '){
				if (game -> player1Sym == 'X'){
					game -> player2Sym = 'O';

				}
				else{
					game -> player2Sym = 'X';
				}

			}
			move -> sym = game -> player2Sym;

		}
		move -> turn = role;

		debug("%ld: parse success", pthread_self());
		sem_post(&game-> seph);
		return move;


	}
	else if (length == 4){
		if (str[length - 1] != 'X' || str[length - 1] != 'O'){
			sem_post(&game-> seph);
			return NULL;
		}
		move -> cord = str[0] - '0';
		move -> sym = str[length - 1];
		move -> turn = role;
		sem_post(&game-> seph);
		return move;

	}
	else{
		sem_post(&game-> seph);
		debug("%ld: fail parse move", pthread_self());
		return NULL;
	}

}

/*
 * Get a string that describes a specified GAME_MOVE, in a format
 * appropriate to be shown to human users.  The returned string should
 * be in a format from which the GAME_MOVE can be recovered by applying
 * game_parse_move() to it.  The returned string is in malloc'ed storage,
 * which it is the responsibility of the caller to free when it is no
 * longer needed.
 *
 * @param move  The GAME_MOVE whose description is to be obtained.
 * @return  A string describing the specified GAME_MOVE.
 */
char *game_unparse_move(GAME_MOVE *move){
	char * m = malloc(sizeof(char)*4);
	m[0] = ((move -> cord) + '0');
	m[1] = '<';
	m[2] = '-';
	m[3] = move -> sym;
	return m;

}