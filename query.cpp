#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

std::string sendQuery(const std::string &ip, int port, const std::string &query) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        return "ERROR: Socket creation failed\n";
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if(inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0){
        close(sock);
        return "ERROR: Invalid address\n";
    }
    
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        close(sock);
        return "ERROR: Connection failed\n";
    }
    
    std::string msg = query + "\n";
    if(send(sock, msg.c_str(), msg.size(), 0) < 0){
        close(sock);
        return "ERROR: Send failed\n";
    }
    
    char buffer[16384];
    std::string response;
    while(true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer)-1, 0);
        if(n <= 0) break;
        buffer[n] = '\0';
        response += buffer;
        if(response.size() > 10000 || response.find("\n\n") != std::string::npos) break;
    }
    
    close(sock);
    return response;
}

int main(int argc, char *argv[]) {
    if(argc < 4) {
        std::cout << "Usage: " << argv[0] << " <server_ip> <port> <option> [node_id]\n\n";
        std::cout << "Options:\n";
        std::cout << "  " << argv[0] << " 127.0.0.1 10035 1       # Cluster stats\n";
        std::cout << "  " << argv[0] << " 127.0.0.1 10035 2 1     # Node 1 data\n";
        return 1;
    }

    std::string ip = argv[1];
    int port = atoi(argv[2]);
    int option = atoi(argv[3]);

    std::string query;

    if(option == 1) {
        
        query = "QUERY STATS";
        std::string response = sendQuery(ip, port, query);
        std::cout << response;
    }
    else if(option == 2) {
        if(argc < 5) {
            std::cout << "ERROR: Node ID required for option 2\n";
            std::cout << "Usage: " << argv[0] << " " << ip << " " << port << " 2 <node_id>\n";
            return 1;
        }
        int node_id = atoi(argv[4]);
        query = "QUERY NODE " + std::to_string(node_id);
        std::string response = sendQuery(ip, port, query);
        std::cout << response;
    }
    else {
        std::cout << "ERROR: Invalid option. Use 1 or 2\n";
        std::cout << "  1           - Get cluster statistics\n";
        std::cout << "  2 <node_id> - Get sensor data for specific node\n";
        return 1;
    }

    return 0;
}