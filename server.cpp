#include <iostream>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <thread>
#include <sstream>

#include "server.h"
#include "raft.h"


void* connection(void* socket_ptr){
    int c_sock = *(int*)socket_ptr;
    free(socket_ptr);

    char buffer[2048];
    std::string accumulated = "";
    
    while(true){
        int bytes = recv(c_sock, buffer, sizeof(buffer)-1, 0);
        if(bytes <= 0){
            close(c_sock);
            return nullptr;
        }
        buffer[bytes] = '\0';
        accumulated += std::string(buffer);
        
        size_t pos;
       
        while((pos = accumulated.find('\n')) != std::string::npos) {
            
            std::string msg = accumulated.substr(0, pos);
            accumulated = accumulated.substr(pos + 1);
            
            if(msg.empty()) continue;

           
            if (msg.rfind("ReqVote", 0) == 0 || msg.rfind("AppendEntries", 0) == 0) {
                extern Raft *graft;

                if (graft) {
                    std::string reply = graft->peerstring(msg);

                    
                    if (!reply.empty() && reply.back() != '\n')
                        reply.push_back('\n');

                    send(c_sock, reply.c_str(), reply.size(), 0);
                }

                
                continue;
            }

            
            if (msg.rfind("HEARTBEAT", 0) == 0 || msg.rfind("DATA", 0) == 0) { 
                extern Raft *graft; 
                
                if (graft) { 
                    bool appended = graft->appendCommand(msg);
                    
                    if (appended) { 
                        std::string ack = "OK replicated\n"; 
                        send(c_sock, ack.c_str(), ack.size(), 0); 
                    } else { 
                        std::string nack = "ERR not_leader\n"; 
                        send(c_sock, nack.c_str(), nack.size(), 0);
                    } 
                } else {
                    std::string nack = "ERR no_raft\n";
                    send(c_sock, nack.c_str(), nack.size(), 0);
                }
            }
            
            else if(msg.rfind("QUERY", 0) == 0){
                extern Raft *graft;
                
                if(!graft){
                    std::string resp = "ERR no_raft\n";
                    send(c_sock, resp.c_str(), resp.size(), 0);
                    continue;
                }
                
                auto tokens = split_ws(msg);
                std::ostringstream response;
                
                if(tokens.size() < 2){
                    response << "ERR invalid_query\n";
                } else {
                    std::string query_type = tokens[1];
                    
                    if(query_type == "STATS"){
                        auto heartbeats = graft->getHeartbeats();
                        auto perNode = graft->getReadingsPerNode();
                        int totalReadings = graft->getTotalSensorReadings();
                        
                        response << "=== CLUSTER STATISTICS ===\n";
                        response << "Total Sensor Readings: " << totalReadings << "\n";
                        
                        int totalHB = 0;
                        for(auto &h : heartbeats) totalHB += h.second;
                        response << "Total Heartbeats: " << totalHB << "\n\n";
                        
                        response << "Per-Node Summary:\n";
                        for(auto &p : perNode) {
                            int nid = p.first;
                            int count = p.second;
                            auto it = heartbeats.find(nid);
                            int hbCount = (it != heartbeats.end()) ? it->second : 0;
                            
                            response << "  Node " << nid << ": " << count << " readings, " 
                                     << hbCount << " heartbeats\n";
                        }
                    }
                    else if(query_type == "NODE" && tokens.size() >= 3){
                        int node_id = atoi(tokens[2].c_str());
                        auto allReadings = graft->getSensorReadingsByNode(node_id);
                        
                        response << "LAST 5 READINGS FOR NODE " << node_id << "\n";
                        response << "Total Readings: " << allReadings.size() << "\n\n";
                        
                        int start = allReadings.size() > 5 ? allReadings.size() - 5 : 0;
                        for(size_t i = start; i < allReadings.size(); i++){
                            response << "  [" << (i - start + 1) << "] Temperature=" 
                                     << allReadings[i].temperature 
                                     << "°C, Humidity=" << allReadings[i].humidity << "%\n";
                        }
                    }
                    else if(query_type == "STATUS"){
                        int logCount = graft->getLogCount();
                        bool isLeader = graft->isLeader();
                        response << "Logs=" << logCount;
                        response << ", Leader=" << (isLeader ? "Yes" : "No") << "\n";
                    }
                    else {
                        response << "ERR unknown_query_type\n";
                    }
                }
                
                std::string resp_str = response.str();
                send(c_sock, resp_str.c_str(), resp_str.size(), 0);
            }
            
            else if(msg.rfind("CMD ",0)==0){
                extern Raft *graft;
                if(graft){
                    if(graft->appendCommand(msg.substr(4))){
                        std::string ack = "OK appended\n";
                        send(c_sock, ack.c_str(), ack.size(), 0);
                    } else {
                        std::string nack = "ERR not_leader\n";
                        send(c_sock, nack.c_str(), nack.size(), 0);
                    }
                } else {
                    std::string r = "ERR no_raft\n";
                    send(c_sock, r.c_str(), r.size(), 0);
                }
            } 
            else {
                std::string ok = "OK\n";
                send(c_sock, ok.c_str(), ok.size(), 0);
            }
        }
    }
}



Raft *graft = nullptr;

int main(int argc, char *argv[]) {
    if(argc < 2){
        std::cout << "Usage: " << argv[0] << " [port] [peer1:port,peer2:port,...] [id]\n";
        std::cout << "Example: ./server 10035 127.0.0.1:10036,127.0.0.1:10037 1\n";
        return 0;
    }

    int port = atoi(argv[1]);
    std::vector<std::string> peers;
    int id = port;
    
    if(argc >= 3){
        std::string peerlist = argv[2];
        size_t start = 0;
        while(true){
            size_t pos = peerlist.find(',', start);
            if(pos==std::string::npos){
                std::string p = peerlist.substr(start);
                if(!p.empty()) peers.push_back(p);
                break;
            } else {
                peers.push_back(peerlist.substr(start, pos-start));
                start = pos+1;
            }
        }
    }
    
    if(argc >=4){
        id = atoi(argv[3]);
    }

    std::cout << "Starting server on port " << port << ", peers:";
    for(auto &p: peers) std::cout << " " << p;
    std::cout << ", id="<<id<<"\n";

    ServerStub1 ServerStub;

    if(!ServerStub.Init(port)){
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    graft = new Raft(id, port, peers);
    graft->start();

    while(1){
        int client_fd = ServerStub.acceptclient();
        if(client_fd < 0) continue;

        pthread_t thread;
        int* pnode = (int*)malloc(sizeof(int));
        *pnode = client_fd;
        if(pthread_create(&thread, nullptr, connection, pnode) != 0){
            perror("ERROR: failed to create thread");
            free(pnode);
            continue;
        }
        pthread_detach(thread);
    }

    delete graft;
    return 0;
}