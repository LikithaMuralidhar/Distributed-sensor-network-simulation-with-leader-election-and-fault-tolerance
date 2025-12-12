#include <iostream>
#include <chrono>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctime>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <vector>
#include <thread>
#include <random>
#include "node.h"

int main(int argc, char *argv[]) {
    if(argc < 5){
        std::cout << "Usage:\n"<< argv[0] << " [ip] [Node_ID] [server_port1 server_port2 ...]\n";
        std::cout << "Example:\n" << argv[0] << " 127.0.0.1 1 10035 10036 10037\n";
        return 0;
    }
    
	std::string ip = argv[1];
    int node_id = atoi(argv[2]);

    std::vector<int> server_ports;
    for(int i = 3; i < argc; i++){
        server_ports.push_back(atoi(argv[i]));
    }

    if(server_ports.empty()){
        std::cerr << "No server ports provided!" << std::endl;
        return 0;
    }

    int current_port_idx = 0;
    int current_port = server_ports[current_port_idx];
    
    Node1 node;
    
    std::cout << "Starting..." << std::endl;
    
    while(1){
        if(node.Init(ip, current_port) == 0) {
    		 
            current_port_idx = (current_port_idx + 1) % server_ports.size();
            current_port = server_ports[current_port_idx];
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        
        std::cout << "[Sensor Node " << node_id << "] Connected to server at port " 
                 << current_port << std::endl;
        
        int sock = node.GetSocket();
        
       
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        
        std::default_random_engine gen(time(NULL) + node_id);
        std::uniform_int_distribution<int> tempDist(20, 35);
        std::uniform_int_distribution<int> humDist(40, 90);
        
        int messageCount = 0;
        bool connected = true;
        
        while (connected) {
           
            std::string hb = "HEARTBEAT node=" + std::to_string(node_id) + "\n";
            
            if (send(sock, hb.c_str(), hb.size(), 0) < 0) {
               
				
                connected = false;
                break;
            }
            
           
			
            char ack_buf[256];
            ssize_t n = recv(sock, ack_buf, sizeof(ack_buf)-1, 0);
            if (n <= 0) {
                
                connected = false;
                break;
            }
            ack_buf[n] = '\0';
            
           
			
            if(strstr(ack_buf, "not_leader") != NULL) {
                
                connected = false;
               
                current_port_idx = (current_port_idx + 1) % server_ports.size();
                current_port = server_ports[current_port_idx];
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            

            int temp = tempDist(gen);
            int hum = humDist(gen);
            
            std::string reading =
                "DATA node=" + std::to_string(node_id) +
                " temp=" + std::to_string(temp) +
                " humidity=" + std::to_string(hum) + "\n";
            
            if (send(sock, reading.c_str(), reading.size(), 0) < 0) {
                std::cout << "Failed to send data" << std::endl;
                connected = false;
                break;
            }
            
            n = recv(sock, ack_buf, sizeof(ack_buf)-1, 0);
            if (n <= 0) {
                std::cout << "No data response" << std::endl;
                connected = false;
                break;
            }
            ack_buf[n] = '\0';
            
            
            if(strstr(ack_buf, "not_leader") != NULL) {
                connected = false;
                
                current_port_idx = (current_port_idx + 1) % server_ports.size();
                current_port = server_ports[current_port_idx];
                break;
            }
            
            messageCount++;
            if(messageCount % 5 == 0) {
                std::cout << "[Sensor Node " << node_id << "] Sent " << messageCount 
                         << " messages to port " << current_port
                         << " (temp=" << temp << "C, humidity=" << hum << "%)" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        
        node.Close();
        std::cout << "[Sensor Node " << node_id << "] Reconnecting..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}