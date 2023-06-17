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

#include "debug.h"
#include "protocol.h"

int proto_send_packet(int fd, JEUX_PACKET_HEADER *hdr, void *data){
    if (hdr == NULL){
        return -1;
    }
	uint16_t datasize = ntohs(hdr->size);
    int bytes_sent = 0;
    // Send header
    while (bytes_sent < sizeof(JEUX_PACKET_HEADER)) {
        int num_bytes = write(fd, hdr + bytes_sent, sizeof(JEUX_PACKET_HEADER));
        if (num_bytes == -1) {
                perror("fail write");
                return -1;
        }
        if (num_bytes == 0){
            break;
        }
        bytes_sent += num_bytes;
    }
    if (datasize > 0){
        debug("%ld: writing payload size of %d", pthread_self(), datasize);
		bytes_sent = 0;
		while (bytes_sent < datasize) {
		    int num_bytes = write(fd, (char*)data + bytes_sent, datasize);
		    if (num_bytes == -1) {
		        perror("fail write");
		        return -1;
		    }
		    if (num_bytes == 0){
		    	break;
		    }
		    bytes_sent += num_bytes;
		}
	}
	return 0;
}
int proto_recv_packet(int fd, JEUX_PACKET_HEADER *hdr, void **payloadp){
    if (hdr == NULL){
        return -1;
    }
    //first read from fd to filld up the header
    int num_bytes = 0;
    int size = sizeof(JEUX_PACKET_HEADER);
    while(num_bytes < size){
        int byte = read(fd, (char*)hdr + num_bytes, size - num_bytes);
        debug("%ld: size  %d %d", pthread_self(), byte, size);
        if (byte == -1) {
            fprintf(stderr, "failed to read payload");
            return -1;
        } else if (byte == 0) {
            break;
        }
        num_bytes += byte;
    }
    if (num_bytes != size){
        return -1;
    }
    uint16_t datasize  = ntohs(hdr->size);

    if (datasize > 0) {
        debug("%ld: reading payload size of %d", pthread_self(), datasize);
        char *payload = calloc(datasize + 1, sizeof(char));
        if (payload == NULL) {
            perror("failed to allocate memory");
            return -1;
        }
        uint16_t bytes_read = 0;
        while (bytes_read < datasize) {
            int num_bytes = read(fd, (payload + bytes_read), datasize - bytes_read);
            if (num_bytes == -1) {
                fprintf(stderr, "failed to read payload");
                return -1;
            } else if (num_bytes == 0) {
                break;
            }
            bytes_read += num_bytes;
        }
        if (bytes_read != datasize){
            debug("%ld: payload size dont match", pthread_self());
            return -1;
        }
        *payloadp = payload;
    }
    else{
        *payloadp =NULL;
    }
    return 0;


}