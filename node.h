#ifndef __NODE_H__
#define __NODE_H__

#include <iostream>
#include <cstring>
#include <arpa/inet.h>   
#include <unistd.h>  
#include <string>

#include <string.h>

#define CLI_RAND_SEED  1234
#define SVR_RAND_SEED  4321
#define BUFFER_SIZE    256

class Node1{
	
private:
    int sockfd;
    struct sockaddr_in addr;

public:
    Node1() : sockfd(-1) {}

int Init(std::string ip, int port){
    

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("ERROR: failed to create socket");
        return 0;
    }
   
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    

    int pton_result = inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    
    
    if(pton_result <= 0) {
        perror("ERROR: invalid IP address");
        close(sockfd);
        return 0;
    }

    
    if(connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0){
        perror("ERROR: failed to connect");
        std::cout << "DEBUG: errno = " << errno << std::endl;
        close(sockfd);
        return 0;
    }
    

    return 1;
}

void Close() {
        if (sockfd != -1) {
            close(sockfd);
            sockfd = -1;
        }
}

int GetSocket() const {
        return sockfd;
    } 
};

#endif
