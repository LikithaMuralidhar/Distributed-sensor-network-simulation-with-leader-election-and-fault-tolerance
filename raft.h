#ifndef __RAFT_H__
#define __RAFT_H__

#include <iostream>
#include <pthread.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <random>
#include <sstream>
#include <atomic>
#include <algorithm>

static inline std::vector<std::string> split_ws(const std::string &s){
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string token;
    while(iss >> token) out.push_back(token);
    return out;
}

enum class role{Follower, Candidate, Leader};

struct Log{
    int term;
    std::string command;
    Log(int t=0, const std::string &c="") : term(t), command(c) {}
};

struct SensorReading {
    int node_id;
    int temperature;
    int humidity;
    int term;
};

class StateMachine {
private:
    std::vector<SensorReading> sensorData;
    std::map<int, int> heartbeatCount;
    std::mutex mu;

public:
    void apply(const Log &log) {
        std::lock_guard<std::mutex> lock(mu);
        
        if(log.command.find("HEARTBEAT") != std::string::npos) {
            size_t pos = log.command.find("node ");
            if(pos == std::string::npos) pos = log.command.find("node=");
            if(pos != std::string::npos) {
                try {
                    std::string substr = log.command.substr(pos + 5);
                    int node_id = std::stoi(substr);
                    heartbeatCount[node_id]++;
                } catch(...) {}
            }
        }
        else if(log.command.find("DATA") != std::string::npos) {
            size_t nodePos = log.command.find("node=");
            size_t tempPos = log.command.find("temp=");
            size_t humPos = log.command.find("humidity=");
            
            if(nodePos != std::string::npos && tempPos != std::string::npos && humPos != std::string::npos) {
                try {
                    int node_id = std::stoi(log.command.substr(nodePos + 5));
                    int temp = std::stoi(log.command.substr(tempPos + 5));
                    int hum = std::stoi(log.command.substr(humPos + 9));
                    
                    SensorReading reading;
                    reading.node_id = node_id;
                    reading.temperature = temp;
                    reading.humidity = hum;
                    reading.term = log.term;
                    sensorData.push_back(reading);
                } catch(...) {}
            }
        }
    }

    std::vector<SensorReading> getAllReadings() {
        std::lock_guard<std::mutex> lock(mu);
        return sensorData;
    }

    std::vector<SensorReading> getSensorReadingsByNode(int nid) {
        std::lock_guard<std::mutex> lock(mu);
        std::vector<SensorReading> res;
        for(auto &r : sensorData)
            if(r.node_id == nid) res.push_back(r);
        return res;
    }

    std::map<int,int> getReadingsPerNode() {
        std::lock_guard<std::mutex> lock(mu);
        std::map<int,int> m;
        for(auto &r : sensorData) m[r.node_id]++;
        return m;
    }

    std::map<int,int> getAllHeartbeats() {
        std::lock_guard<std::mutex> lock(mu);
        return heartbeatCount;
    }

    int getTotalReadings() {
        std::lock_guard<std::mutex> lock(mu);
        return sensorData.size();
    }
};


class Raft{
private:
    int me;
    int listen_port;
    std::vector<std::string> peer_addrs;

    int currentterm;
    int votedfor;

    std::vector<Log> logs;  
    int commitindex;
    int lastapplied;

    role role1;
    std::mutex mu;
    std::thread raftThread;
    std::thread applyThread;
    std::atomic<bool> stopflag;
    std::chrono::steady_clock::time_point lastHeartbeat;
    std::mt19937 rng;

    int totalMessages;
    int totalElections;
    std::vector<long long> electionTimes;

    std::mutex metricsMutex;

    StateMachine stateMachine;

public:
    Raft(int id, int port, const std::vector<std::string>& peers)
      : me(id), listen_port(port), peer_addrs(peers),
        currentterm(0), votedfor(-1),
        commitindex(0), lastapplied(0),
        role1(role::Follower), stopflag(false),
        totalMessages(0), totalElections(0)
    {
        rng.seed(std::random_device{}());
        lastHeartbeat = std::chrono::steady_clock::now();
    }

    ~Raft() { stop(); }

    void applyCommittedEntries(){
        while(!stopflag){
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::lock_guard<std::mutex> lk(mu);
            while(lastapplied < commitindex && lastapplied < (int)logs.size()){
                lastapplied++;
                Log log = logs[lastapplied-1];
                lk.~lock_guard();
                stateMachine.apply(log);
                new (&lk) std::lock_guard<std::mutex>(mu);
            }
        }
    }

    bool start(){
        raftThread = std::thread(&Raft::raftloop, this);
        applyThread = std::thread(&Raft::applyCommittedEntries, this);
        return true;
    }

    void stop(){
        stopflag = true;
        if(raftThread.joinable()) raftThread.join();
        if(applyThread.joinable()) applyThread.join();
    }

    
    
    std::string peerstring(const std::string &msg){
        auto t = split_ws(msg);
        if(t.empty()) return "ERR\n";

        
        
        if(t[0] == "ReqVote"){
            int term = stoi(t[1]);
            int candidate = stoi(t[2]);
            int lastLogIndex = stoi(t[3]);
            int lastLogTerm  = stoi(t[4]);

            std::lock_guard<std::mutex> lk(mu);

            if(term > currentterm){
                currentterm = term;
                role1 = role::Follower;
                votedfor = -1;
            }

            bool grant = false;

            if(term >= currentterm &&
              (votedfor == -1 || votedfor == candidate)){

                int myLastIdx = logs.size();
                int myLastTerm = (myLastIdx>0 ? logs[myLastIdx-1].term : 0);

                bool uptodate =
                    (lastLogTerm > myLastTerm) ||
                    (lastLogTerm == myLastTerm && lastLogIndex >= myLastIdx);

                if(uptodate){
                    grant = true;
                    votedfor = candidate;
                    lastHeartbeat = std::chrono::steady_clock::now();
                }
            }

            std::ostringstream out;
            out << "ReqVote_RESP " << currentterm << " " << (grant?1:0);
            return out.str();
        }

        
        
        if(t[0] == "AppendEntries"){
            int term = stoi(t[1]);
            int leader = stoi(t[2]);
            int prevIdx = stoi(t[3]);
            int prevTerm = stoi(t[4]);
            int leaderCommit = stoi(t[5]);
            int count = stoi(t[6]);

            std::lock_guard<std::mutex> lk(mu);
            bool success = true;

            if(term < currentterm){
                success = false;
            } else {
                if(term > currentterm){
                    currentterm = term;
                    role1 = role::Follower;
                    votedfor = -1;
                }
                lastHeartbeat = std::chrono::steady_clock::now();
                role1 = role::Follower;

                
                if(prevIdx == -1){
                    logs.clear();
                }
                else {
                    if(prevIdx > (int)logs.size()) success = false;
                    else if(prevIdx > 0 && logs[prevIdx-1].term != prevTerm) success = false;
                }

                if(success){
                    if(prevIdx >= 0 && prevIdx < (int)logs.size()){
                        logs.erase(logs.begin() + prevIdx, logs.end());
                    }

                    for(int i = 0; i < count; i++){
                        std::string e = t[7+i];
                        auto pos = e.find('|');
                        int et = stoi(e.substr(0,pos));
                        std::string cmd = e.substr(pos+1);
                        logs.emplace_back(et, cmd);
                    }

                    if(leaderCommit > commitindex){
                        commitindex = std::min(leaderCommit, (int)logs.size());
                    }
                }
            }

            std::ostringstream out;
            out << "AppendEntries_RESP " << currentterm << " " << (success?1:0);
            return out.str();
        }

        return "ERR\n";
    }

    
    
    void raftloop(){
        using namespace std::chrono;

        std::uniform_int_distribution<int> dist(150,300);
        int timeoutMs = dist(rng);

        while(!stopflag){
            std::this_thread::sleep_for(milliseconds(10));
            auto now = steady_clock::now();

            std::unique_lock<std::mutex> lk(mu);

            
            if(role1 == role::Leader){
               
                std::vector<Log> copy = logs;
                int commit = commitindex;
                int term   = currentterm;

                auto peers = peer_addrs;
                lk.unlock();

                for(auto &p : peers){
                    std::ostringstream req;

                    req << "AppendEntries " 
                        << term << " " << me << " "
                        << -1 << " " << 0 << " "
                        << commit << " " << (int)copy.size();

                    for (auto &entry : copy) {
                          
                      std::string safeCmd = entry.command;
                      std::replace(safeCmd.begin(), safeCmd.end(), ' ', '~');
                      req << " " << entry.term << "|" << safeCmd;
                    }

                    std::string resp = msgtopeer(p, req.str());
                    
                }

                std::this_thread::sleep_for(milliseconds(100));
                continue;
            }

          
            auto msSince = duration_cast<milliseconds>(now - lastHeartbeat).count();

            if(msSince >= timeoutMs){
                role1 = role::Candidate;
                currentterm++;
                votedfor = me;
                int thisTerm = currentterm;

                lk.unlock();

                int votes = 1;
                auto peers = peer_addrs;
                auto electionStart = steady_clock::now();
                int need = (peers.size()+1)/2 + 1;

                int lastIdx = logs.size();
                int lastTerm = (lastIdx>0 ? logs[lastIdx-1].term : 0);

                for(auto &p : peers){
                    std::ostringstream req;
                    req << "ReqVote " << thisTerm << " " << me
                        << " " << lastIdx << " " << lastTerm;

                    auto resp = msgtopeer(p, req.str());
                    auto tok = split_ws(resp);

                    if(tok.size() >= 3 && tok[0] == "ReqVote_RESP"){
                        int rterm = stoi(tok[1]);
                        int granted = stoi(tok[2]);

                        std::lock_guard<std::mutex> lk2(mu);
                        if(rterm > currentterm){
                            currentterm = rterm;
                            role1 = role::Follower;
                            votedfor = -1;
                        }
                        else if(rterm == thisTerm && role1 == role::Candidate && granted==1){
                            votes++;
                        }

                        if(votes >= need && role1 == role::Candidate && currentterm==thisTerm){
                            
                            role1 = role::Leader;
                            lastHeartbeat = steady_clock::now();
                            {
                                std::lock_guard<std::mutex> mm(metricsMutex);
                                totalElections++;
                            }
                        }
                    }
                }

                lk.lock();
                timeoutMs = dist(rng);
            }

            lk.unlock();
        }
    }

    
    std::string msgtopeer(const std::string &peerAddr, const std::string &msg){
        size_t pos = peerAddr.find(':');
        if(pos == std::string::npos) return "";

        std::string ip = peerAddr.substr(0,pos);
        int port = stoi(peerAddr.substr(pos+1));

        int s = socket(AF_INET, SOCK_STREAM, 0);
        if(s < 0) return "";

        sockaddr_in a;
        memset(&a,0,sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &a.sin_addr);

        struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if(connect(s,(sockaddr*)&a,sizeof(a)) < 0){
            close(s);
            return "";
        }

        std::string out = msg;
        if(out.back() != '\n') out.push_back('\n');
        send(s, out.c_str(), out.size(), 0);

        char buf[8192];
        int r = recv(s, buf, sizeof(buf)-1, 0);
        if(r <= 0){
            close(s);
            return "";
        }
        buf[r] = '\0';
        close(s);
        return std::string(buf);
    }

   
    bool appendCommand(const std::string &cmd){
        std::lock_guard<std::mutex> lk(mu);
        if(role1 != role::Leader) return false;
        logs.emplace_back(currentterm, cmd);
        commitindex = logs.size();
        return true;
    }

    std::vector<SensorReading> getSensorReadings() {
        return stateMachine.getAllReadings();
    }
    std::vector<SensorReading> getSensorReadingsByNode(int nid){
        return stateMachine.getSensorReadingsByNode(nid);
    }
    std::map<int,int> getReadingsPerNode(){
        return stateMachine.getReadingsPerNode();
    }
    int getTotalSensorReadings(){
        return stateMachine.getTotalReadings();
    }
    std::map<int,int> getHeartbeats(){
        return stateMachine.getAllHeartbeats();
    }
    int getLogCount(){
        std::lock_guard<std::mutex> lk(mu);
        return logs.size();
    }
    bool isLeader(){
        std::lock_guard<std::mutex> lk(mu);
        return role1 == role::Leader;
    }
};

#endif
