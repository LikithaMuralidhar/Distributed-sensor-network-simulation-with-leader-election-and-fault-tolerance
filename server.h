#ifndef __SERVER_H__
#define __SERVER_H__

#include <iostream>

#include <assert.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>

#define BUFFER_SIZE 256
#define BACKLOG	256



class ServerStub1{

    private:
    int sockfd,newfd;
    struct sockaddr_in addr;

public:
    ServerStub1() : sockfd(-1), newfd(-1) {}


    int Init(int port){
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("ERROR: failed to create socket");
		return 0;
	}

    int opt = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("ERROR: setsockopt failed");
            close(sockfd);
            sockfd = -1;
            return 0;
        }

	memset(&addr, '\0', sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;

    
	if ((bind(sockfd, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
		perror("ERROR: failed to bind");
		return 0;
	}

    listen(sockfd, BACKLOG);

    
  return 1;
    
}
int acceptclient(){

    socklen_t addr_size = sizeof(addr);
    newfd = accept(sockfd, (struct sockaddr *) &addr, &addr_size);
    if (newfd < 0) {
        perror("ERROR: failed to accept");
        return -1;  
    }
    std::cout << "Client connected successfully!" << std::endl;
    return newfd;

}
void SetClientFD(int fd) { newfd = fd; }

};

#endif