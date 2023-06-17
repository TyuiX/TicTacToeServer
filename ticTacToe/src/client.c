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

typedef struct client{
	int fd;
	int ref ;
	PLAYER *playerRef;
	INVITATION * listOfInv[MAX_CLIENTS];
	sem_t seph;

} CLIENT;

/*
 * Create a new CLIENT object with a specified file descriptor with which
 * to communicate with the client.  The returned CLIENT has a reference
 * count of one and is in the logged-out state.
 *
 * @param creg  The client registry in which to create the client.
 * @param fd  File descriptor of a socket to be used for communicating
 * with the client.
 * @return  The newly created CLIENT object, if creation is successful,
 * otherwise NULL.
 */
CLIENT *client_create(CLIENT_REGISTRY *creg, int fd){
	if (creg == NULL){
		return NULL;
	}
	CLIENT *c = malloc(sizeof(CLIENT));
	c->ref = 1;
	c->fd = fd;
	c->playerRef = NULL;  //Player is considered logged out when client's player is NULL
	for (int i = 0; i < MAX_CLIENTS; i++) {
		c ->listOfInv[i] = NULL;  //Set all invitations to NULL empty
	}
	sem_init(&c->seph, 0, 1);
	return c;
}

/*
 * Increase the reference count on a CLIENT by one.
 *
 * @param client  The CLIENT whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same CLIENT that was passed as a parameter.
 */
CLIENT *client_ref(CLIENT *client, char *why){
	sem_wait(&client -> seph);
	if (client == NULL){
		sem_post(&client -> seph);
		return NULL;
	}
	debug("%ld: %p %s", pthread_self(), client, why);
	client -> ref += 1;
	sem_post(&client -> seph);
	return client;
}
/*
 * Decrease the reference count on a CLIENT by one.  If after
 * decrementing, the reference count has reached zero, then the CLIENT
 * and its contents are freed.
 *
 * @param client  The CLIENT whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void client_unref(CLIENT *client, char *why){
	if (client != NULL){
		sem_wait(&client -> seph);
		debug("%ld: %p %s", pthread_self(), client, why);
		if (client != NULL){
			client -> ref -= 1;
			if (client -> ref == 0){
				sem_post(&client -> seph);
				free(client);
			}
			else{
				sem_post(&client -> seph);
			}
		}
		else{
			sem_post(&client -> seph);
		}
	}
}
/*
 * Log in this CLIENT as a specified PLAYER.
 * The login fails if the CLIENT is already logged in or there is already
 * some other CLIENT that is logged in as the specified PLAYER.
 * Otherwise, the login is successful, the CLIENT is marked as "logged in"
 * and a reference to the PLAYER is retained by it.  In this case,
 * the reference count of the PLAYER is incremented to account for the
 * retained reference.
 *
 * @param CLIENT  The CLIENT that is to be logged in.
 * @param PLAYER  The PLAYER that the CLIENT is to be logged in as.
 * @return 0 if the login operation is successful, otherwise -1.
 */
int client_login(CLIENT *client, PLAYER *player){
	sem_wait(&client -> seph);
	if (client == NULL || player == NULL){
		sem_post(&client -> seph);
		return -1;
	}
	if (client -> playerRef != NULL){
		sem_post(&client -> seph);
		return -1;
	}
	CLIENT*temp = creg_lookup(client_registry, player_get_name(player));
	if (temp != NULL){
		sem_post(&client -> seph);
		return -1;
	}
	client_unref(temp, "dereferencing after look up for login checking");
	client -> playerRef = player;
	player_ref(player, "logging into a client");
	sem_post(&client -> seph);
	return 0;
}


/*
 * Log out this CLIENT.  If the client was not logged in, then it is
 * an error.  The reference to the PLAYER that the CLIENT was logged
 * in as is discarded, and its reference count is decremented.  Any
 * INVITATIONs in the client's list are revoked or declined, if
 * possible, any games in progress are resigned, and the invitations
 * are removed from the list of this CLIENT as well as its opponents'.
 *
 * @param client  The CLIENT that is to be logged out.
 * @return 0 if the client was logged in and has been successfully
 * logged out, otherwise -1.
 */
int client_logout(CLIENT *client){
	sem_wait(&client -> seph);
	if (client -> playerRef == NULL){
		sem_post(&client -> seph);
		return -1;
	}
	for (int i = 0; i < MAX_CLIENTS; i++) {
		/* Resign if a game is in progress */
		if (client-> listOfInv[i] != NULL) {
			GAME * g = inv_get_game(client-> listOfInv[i]);
			if (g != NULL) {  
				client_resign_game(client, i);
			}
			else{
				client_revoke_invitation(client, i);
			}
			//client_remove_invitation(client, client-> listOfInv[i]);
		}
	}
	player_unref(client -> playerRef, "logging out of client");
	client -> playerRef = NULL;
	sem_post(&client -> seph);
	return 0;
}


/*
 * Get the PLAYER for the specified logged-in CLIENT.
 * The reference count on the returned PLAYER is NOT incremented,
 * so the returned reference should only be regarded as valid as long
 * as the CLIENT has not been freed.
 *
 * @param client  The CLIENT from which to get the PLAYER.
 * @return  The PLAYER that the CLIENT is currently logged in as,
 * otherwise NULL if the player is not currently logged in.
 */
PLAYER *client_get_player(CLIENT *client){
	if (client == NULL){
		return NULL;
	}
	if (client -> playerRef == NULL){
		return NULL;
	}
	return client -> playerRef;
}


/*
 * Get the file descriptor for the network connection associated with
 * this CLIENT.
 *
 * @param client  The CLIENT for which the file descriptor is to be
 * obtained.
 * @return the file descriptor.
 */
int client_get_fd(CLIENT *client){
	if (client == NULL){
		return -1;
	}
	return client -> fd;
}


/*
 * Send a packet to a client.  Exclusive access to the network connection
 * is obtained for the duration of this operation, to prevent concurrent
 * invocations from corrupting each other's transmissions.  To prevent
 * such interference, only this function should be used to send packets to
 * the client, rather than the lower-level proto_send_packet() function.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param pkt  The header of the packet to be sent.
 * @param data  Data payload to be sent, or NULL if none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */	
int client_send_packet(CLIENT *player, JEUX_PACKET_HEADER *pkt, void *data){
	if (player == NULL){
		return -1;
	}
	debug("%ld: sending %s", pthread_self(), (char *)data);
	sem_wait(&player -> seph);
	struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
	pkt -> timestamp_sec = htonl(current_time.tv_sec);
	pkt -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (proto_send_packet(client_get_fd(player), pkt, data)) {
		sem_post(&player -> seph);
		return -1;
	}
	sem_post(&player -> seph);
	return 0;
}
/*
 * Send an ACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param data  Pointer to the optional data payload for this packet,
 * or NULL if there is to be no payload.
 * @param datalen  Length of the data payload, or 0 if there is none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_ack(CLIENT *client, void *data, size_t datalen){
	if (client == NULL){
		return -1;
	}
	sem_wait(&client -> seph);
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr -> type = JEUX_ACK_PKT;
	hdr -> size = htons(datalen);
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (proto_send_packet(client -> fd, hdr, data)){
		sem_post(&client -> seph);
		return -1;
	}
	debug("%ld: send ack success", pthread_self());
	sem_post(&client -> seph);
	return 0;
}

/*
 * Send an NACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_nack(CLIENT *client){
	if (client == NULL){
		return -1;
	}
	sem_wait(&client -> seph);
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr -> type = JEUX_NACK_PKT;
	hdr -> size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (proto_send_packet(client -> fd, hdr, NULL)){
		sem_post(&client -> seph);
		return -1;
	}
	sem_post(&client -> seph);
	return 0;
}

/*
 * Add an INVITATION to the list of outstanding invitations for a
 * specified CLIENT.  A reference to the INVITATION is retained by
 * the CLIENT and the reference count of the INVITATION is
 * incremented.  The invitation is assigned an integer ID,
 * which the client subsequently uses to identify the invitation.
 *
 * @param client  The CLIENT to which the invitation is to be added.
 * @param inv  The INVITATION that is to be added.
 * @return  The ID assigned to the invitation, if the invitation
 * was successfully added, otherwise -1.
 */
int client_add_invitation(CLIENT *client, INVITATION *inv){
	if (client == NULL){
		return -1;
	}
	debug("%ld: adding inv %p", pthread_self(), inv);
	if (inv == NULL){
		return -1;
	}
	sem_wait(&client -> seph);
	for (int i = 0; i < MAX_CLIENTS; i++ ){
		if (client -> listOfInv[i] == NULL){
			client -> listOfInv[i] = inv;
			inv_ref(inv, "added to client inv list");
			sem_post(&client -> seph);
			return i;
		}

	}
	sem_post(&client -> seph);
	return -1;
}
/*
 * Remove an invitation from the list of outstanding invitations
 * for a specified CLIENT.  The reference count of the invitation is
 * decremented to account for the discarded reference.
 *
 * @param client  The client from which the invitation is to be removed.
 * @param inv  The invitation that is to be removed.
 * @return the CLIENT's id for the INVITATION, if it was successfully
 * removed, otherwise -1.
 */
int client_remove_invitation(CLIENT *client, INVITATION *inv){
	if (client == NULL){
		return -1;
	}
	sem_wait(&client -> seph);
	for (int i = 0; i < MAX_CLIENTS; i++ ){
		if (client -> listOfInv[i] == inv){
			client -> listOfInv[i] = NULL;
			inv_ref(inv, "removing from client inv list");
			sem_post(&client -> seph);
			return i;
		}

	}
	sem_post(&client -> seph);
	return -1;
}

/*
 * Make a new invitation from a specified "source" CLIENT to a specified
 * target CLIENT.  The invitation represents an offer to the target to
 * engage in a game with the source.  The invitation is added to both the
 * source's list of invitations and the target's list of invitations and
 * the invitation's reference count is appropriately increased.
 * An `INVITED` packet is sent to the target of the invitation.
 *
 * @param source  The CLIENT that is the source of the INVITATION.
 * @param target  The CLIENT that is the target of the INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of the INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of the INVITATION.
 * @return the ID assigned by the source to the INVITATION, if the operation
 * is successful, otherwise -1.
 */
int client_make_invitation(CLIENT *source, CLIENT *target,
	GAME_ROLE source_role, GAME_ROLE target_role){
	INVITATION *inv =  inv_create(source, target, source_role, target_role);
	int sourceId= client_add_invitation(source, inv);
	int targetId = client_add_invitation(target, inv);
	if (sourceId == -1){
		return -1;
	}
	else if (targetId == -1){
		return -1;
	}
	//sending packet accepted packet to the target client
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr -> type = JEUX_INVITED_PKT;
	hdr -> id = targetId;
	hdr -> role = target_role;
	hdr -> size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(target, hdr, NULL)){
		free(hdr);
		return -1;
	}
	free(hdr);
	return sourceId;

}

/*
 * Revoke an invitation for which the specified CLIENT is the source.
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the source
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * revoked is in a state other than the "open" state.  If the invitation
 * is successfully revoked, then the target is sent a REVOKED packet
 * containing the target's ID of the revoked invitation.
 *
 * @param client  The CLIENT that is the source of the invitation to be
 * revoked.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * revoked.
 * @return 0 if the invitation is successfully revoked, otherwise -1.
 */
int client_revoke_invitation(CLIENT *client, int id){
	if (client == NULL){
		return -1;
	}
	if (client -> listOfInv[id] == NULL){
		return -1;
	}
	if (inv_close(client -> listOfInv[id], NULL_ROLE)){
		return -1;
	}
	if (inv_get_source(client -> listOfInv[id]) != client){
		return -1;
	}
	CLIENT * ct = inv_get_target(client -> listOfInv[id]);
	if (client_remove_invitation(client, client -> listOfInv[id]) == -1){
		return -1;
	}
	int ctid;
	if ((ctid = client_remove_invitation(ct, client -> listOfInv[id])) == -1){
		return -1;
	}
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr->type = JEUX_REVOKED_PKT;
	hdr->id = ctid;
	hdr->size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(ct, hdr, NULL)) {
		free(hdr);
		return -1;
	}
	free(hdr);
	return 0;

}

/*
 * Decline an invitation previously made with the specified CLIENT as target.  
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the target
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * declined is in a state other than the "open" state.  If the invitation
 * is successfully declined, then the source is sent a DECLINED packet
 * containing the source's ID of the declined invitation.
 *
 * @param client  The CLIENT that is the target of the invitation to be
 * declined.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * declined.
 * @return 0 if the invitation is successfully declined, otherwise -1.
 */
int client_decline_invitation(CLIENT *client, int id){
	if (client == NULL){
		return -1;
	}
	if (client -> listOfInv[id] == NULL){
		return -1;
	}
	CLIENT * ct = inv_get_source(client -> listOfInv[id]);
	if (inv_get_target(client -> listOfInv[id]) != client){
		return -1;
	}
	if (client_remove_invitation(client, client -> listOfInv[id]) == -1){
		return -1;
	}
	if (ct == NULL){
		return -1;
	}
	int ctid;
	if ((ctid = client_remove_invitation(ct, client -> listOfInv[id])) == -1){
		return -1;
	}
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr->type = JEUX_DECLINED_PKT;
	hdr->id = ctid;
	hdr->size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(ct, hdr, NULL)) {
		free(hdr);
		return -1;
	}
	free(hdr);
	return 0;

}
/*
 * Accept an INVITATION previously made with the specified CLIENT as
 * the target.  A new GAME is created and a reference to it is saved
 * in the INVITATION.  If the invitation is successfully accepted,
 * the source is sent an ACCEPTED packet containing the source's ID
 * of the accepted INVITATION.  If the source is to play the role of
 * the first player, then the payload of the ACCEPTED packet contains
 * a string describing the initial game state.  A reference to the
 * new GAME (with its reference count incremented) is returned to the
 * caller.
 *
 * @param client  The CLIENT that is the target of the INVITATION to be
 * accepted.
 * @param id  The ID assigned by the target to the INVITATION.
 * @param strp  Pointer to a variable into which will be stored either
 * NULL, if the accepting client is not the first player to move,
 * or a malloc'ed string that describes the initial game state,
 * if the accepting client is the first player to move.
 * If non-NULL, this string should be used as the payload of the `ACK`
 * message to be sent to the accepting client.  The caller must free
 * the string after use.
 * @return 0 if the INVITATION is successfully accepted, otherwise -1.
 */
int client_accept_invitation(CLIENT *client, int id, char **strp){
	if (client == NULL){
		return -1;
	}
	if (id < 0){
		return -1;
	}
	if (client->listOfInv[id] == NULL){
		return -1;
	}
	if (inv_accept(client->listOfInv[id] )){
		return -1;
	}
	CLIENT *otherC = inv_get_source(client->listOfInv[id]);
	if (otherC == client){
		return -1;
	}
	int gid = -1;
	for (int i = 0; i < MAX_CLIENTS; i++){
		if (otherC -> listOfInv[i] == client -> listOfInv[id]){
			gid = i;
			break;
		}
	}
	if (gid == -1){
		return -1;
	}
	char * gs = game_unparse_state(inv_get_game(client -> listOfInv[id]));
	debug("%ld: game state %s", pthread_self(), gs);
	if (inv_get_source_role(client -> listOfInv[id]) == FIRST_PLAYER_ROLE){
		JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
		hdr->type = JEUX_ACCEPTED_PKT;
		hdr->id = gid;
		hdr->size = htons(strlen(gs));
		debug("%ld: statelength %ld", pthread_self(), strlen(gs));
		struct timespec current_time;
		clock_gettime(CLOCK_REALTIME, &current_time);
		hdr -> timestamp_sec = htonl(current_time.tv_sec);
		hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
		if (client_send_packet(otherC, hdr, gs)) {
			free(hdr);
			return -1;
		}
		free(hdr);
		debug("%ld: sending game state to opp", pthread_self());
		*strp = NULL;
		return 0;
	}
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr->type = JEUX_ACCEPTED_PKT;
	hdr->id = gid;
	hdr->size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(otherC, hdr, NULL)) {
		free(hdr);
		return -1;
	}
	*strp = gs;
	free(hdr);
	debug("%ld: sending game state to self", pthread_self());
	return 0;
}

/*
 * Resign a game in progress.  This function may be called by a CLIENT
 * that is either source or the target of the INVITATION containing the
 * GAME that is to be resigned.  It is an error if the INVITATION containing
 * the GAME is not in the ACCEPTED state.  If the game is successfully
 * resigned, the INVITATION is set to the CLOSED state, it is removed
 * from the lists of both the source and target, and a RESIGNED packet
 * containing the opponent's ID for the INVITATION is sent to the opponent
 * of the CLIENT that has resigned.
 *
 * @param client  The CLIENT that is resigning.
 * @param id  The ID assigned by the CLIENT to the INVITATION that contains
 * the GAME to be resigned.
 * @return 0 if the game is successfully resigned, otherwise -1.
 */
int client_resign_game(CLIENT *client, int id){
	if (client == NULL){
		return -1;
	}
	if (id < 0){
		return -1;
	}
	if (client -> listOfInv[id] == NULL){
		return -1;
	}
	int role;
	CLIENT *otherC;
	if (inv_get_target(client -> listOfInv[id]) == client){
		otherC = inv_get_source(client -> listOfInv[id]);
		role = inv_get_target_role(client -> listOfInv[id]);
	}
	else{
		role = inv_get_source_role(client -> listOfInv[id]);
		otherC = inv_get_target(client -> listOfInv[id]);
	}
	if (role == FIRST_PLAYER_ROLE){
		player_post_result(client_get_player(client), client_get_player(otherC), 2);
	}
	else{
		player_post_result(client_get_player(otherC), client_get_player(client), 1);
	}
	inv_close(client -> listOfInv[id], role);
	if (client_remove_invitation(client, client -> listOfInv[id]) == -1){
		return -1;
	}
	int otherId = client_remove_invitation(otherC, client -> listOfInv[id]);
	if (otherId == -1){
		return -1;
	}
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr->type = JEUX_RESIGNED_PKT;
	hdr->id = otherId;
	hdr->size = 0;
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(otherC, hdr, NULL)) {
		free(hdr);
		return -1;
	}
	free(hdr);
	return 0;

}

/*
 * Make a move in a game currently in progress, in which the specified
 * CLIENT is a participant.  The GAME in which the move is to be made is
 * specified by passing the ID assigned by the CLIENT to the INVITATION
 * that contains the game.  The move to be made is specified as a string
 * that describes the move in a game-dependent format.  It is an error
 * if the ID does not refer to an INVITATION containing a GAME in progress,
 * if the move cannot be parsed, or if the move is not legal in the current
 * GAME state.  If the move is successfully made, then a MOVED packet is
 * sent to the opponent of the CLIENT making the move.  In addition, if
 * the move that has been made results in the game being over, then an
 * ENDED packet containing the appropriate game ID and the game result
 * is sent to each of the players participating in the game, and the
 * INVITATION containing the now-terminated game is removed from the lists
 * of both the source and target.  The result of the game is posted in
 * order to update both players' ratings.
 *
 * @param client  The CLIENT that is making the move.
 * @param id  The ID assigned by the CLIENT to the GAME in which the move
 * is to be made.
 * @param move  A string that describes the move to be made.
 * @return 0 if the move was made successfully, -1 otherwise.
 */
int client_make_move(CLIENT *client, int id, char *move){
	if (client == NULL){
		return -1;
	}
	if (client -> listOfInv[id] == NULL){
		return -1;
	}
	GAME *g = inv_get_game(client -> listOfInv[id]);
	if (g == NULL){
		return -1;
	}
	int role;
	CLIENT *otherC;
	if (inv_get_target(client -> listOfInv[id]) == client){
		otherC = inv_get_source(client -> listOfInv[id]);
		role = inv_get_target_role(client -> listOfInv[id]);
	}
	else{
		role = inv_get_source_role(client -> listOfInv[id]);
		otherC = inv_get_target(client -> listOfInv[id]);
	}
	GAME_MOVE * m = game_parse_move(g, role, move);
	if (game_apply_move(g, m)){
		debug("%ld: fail apply move", pthread_self());
		return -1;
	}
	if (game_is_over(g)){
		GAME_ROLE winner = game_get_winner(g);
		CLIENT *source = inv_get_source(client -> listOfInv[id]);
		CLIENT *target= inv_get_target(client -> listOfInv[id]);
		GAME_ROLE sR = inv_get_source_role(client -> listOfInv[id]);
		if (sR == FIRST_PLAYER_ROLE){
			player_post_result(client_get_player(source), client_get_player(target), winner);
		}
		else{
			player_post_result(client_get_player(target), client_get_player(source), winner);
		}
		JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
		int temp = client_remove_invitation(source, client -> listOfInv[id]);
		if (temp == -1){
			debug("%ld: fail remove inv", pthread_self());
			return -1;
		}
		hdr->type = JEUX_ENDED_PKT;
		hdr->id = temp;
		hdr->size = 0;
		struct timespec current_time;
		clock_gettime(CLOCK_REALTIME, &current_time);
		hdr -> timestamp_sec = htonl(current_time.tv_sec);
		hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
		if (client_send_packet(source, hdr, NULL)) {
			free(hdr);
			return -1;
		}
		temp = client_remove_invitation(target, client -> listOfInv[id]);
		hdr->id = temp;
		if (client_send_packet(target, hdr, NULL)) {
			free(hdr);
			return -1;
		}
		free(hdr);
		return 0;


	}
	int gid = -1;
	for (int i = 0; i < MAX_CLIENTS; i++){
		if (otherC -> listOfInv[i] == client -> listOfInv[id]){
			gid = i;
			break;
		}
	}
	if (gid == -1){
		debug("%ld: fail get inv id", pthread_self());
		return -1;
	}
	char * state = game_unparse_state(g);
	JEUX_PACKET_HEADER *hdr = calloc(1, sizeof(JEUX_PACKET_HEADER));
	hdr->type = JEUX_MOVED_PKT;
	hdr->id = gid;
	hdr->size = htons(18);
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);
	hdr -> timestamp_sec = htonl(current_time.tv_sec);
	hdr -> timestamp_nsec = htonl(current_time.tv_nsec);
	if (client_send_packet(otherC, hdr, state)){
		free(hdr);
		debug("%ld: fail send", pthread_self());
		return -1;
	}
	client_send_ack(client, NULL, 0);
	free(hdr);
	debug("%ld: send successfuly", pthread_self());
	return 0;
}