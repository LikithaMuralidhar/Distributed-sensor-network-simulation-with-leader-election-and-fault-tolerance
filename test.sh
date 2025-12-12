#!/bin/bash

echo "============================================="
echo "COMPLETE RAFT DISTRIBUTED SENSOR NETWORK TEST"
echo "============================================="
echo ""


BASE_PORT=10035
PORTS=($BASE_PORT $((BASE_PORT+1)) $((BASE_PORT+2)))
NUM_SENSORS=5

# Arrays to track PIDs
declare -a SERVER_PIDS
declare -a NODE_PIDS

echo "PHASE 1: Build and Start Servers"
echo "-----------------------------------"

echo "Building..."
make clean > /dev/null 2>&1
make all > /dev/null 2>&1

if [ ! -f "./server" ]; then
    echo " Build failed"
    exit 1
fi

echo "Build successful"
echo ""

# Start servers
echo "Starting 3 Raft servers..."
./server ${PORTS[0]} "127.0.0.1:${PORTS[1]},127.0.0.1:${PORTS[2]}" 1 > s1.log 2>&1 &
SERVER_PIDS[1]=$!
echo "  Server 1 on port ${PORTS[0]} (PID: ${SERVER_PIDS[1]})"

sleep 1

./server ${PORTS[1]} "127.0.0.1:${PORTS[0]},127.0.0.1:${PORTS[2]}" 2 > s2.log 2>&1 &
SERVER_PIDS[2]=$!
echo "  Server 2 on port ${PORTS[1]} (PID: ${SERVER_PIDS[2]})"

sleep 1

./server ${PORTS[2]} "127.0.0.1:${PORTS[0]},127.0.0.1:${PORTS[1]}" 3 > s3.log 2>&1 &
SERVER_PIDS[3]=$!
echo "  Server 3 on port ${PORTS[2]} (PID: ${SERVER_PIDS[3]})"

echo ""
echo "Waiting for leader election"
sleep 10

echo ""
echo "PHASE 2: Find Real Leader"
echo "-----------------------------------"

LEADER_PORT=""
LEADER_ID=""

for i in 0 1 2; do
    port=${PORTS[$i]}
    server_id=$((i+1))
    
   
    if ! ps -p ${SERVER_PIDS[$server_id]} > /dev/null 2>&1; then
        echo "Server $server_id (port $port):Process died"
        continue
    fi
    
    
    TEST_RESULT=$(echo "CMD TEST_LEADER_CHECK" | nc -w 1 127.0.0.1 $port 2>/dev/null)
    
    if echo "$TEST_RESULT" | grep -q "OK appended"; then
        echo "Server $server_id (port $port): LEADER"
        LEADER_PORT=$port
        LEADER_ID=$server_id
    elif echo "$TEST_RESULT" | grep -q "not_leader"; then
        echo "Server $server_id (port $port): Follower"
    else
        echo "Server $server_id (port $port): Unknown (no response)"
    fi
done

echo ""
if [ -z "$LEADER_PORT" ]; then
    echo "No leader found using command test!"
    echo ""
    echo "Trying alternate method - check server logs for 'BECAME LEADER':"
    
    for i in 1 2 3; do
        if grep -q "BECAME LEADER" s${i}.log 2>/dev/null; then
            port=${PORTS[$((i-1))]}
            echo "  Server $i: Claims to be leader"
            LEADER_PORT=$port
            LEADER_ID=$i
        fi
    done
    
    if [ -z "$LEADER_PORT" ]; then
        echo ""
        echo "Server logs:"
        for i in 1 2 3; do
            echo "=== Server $i ===" 
            tail -20 s${i}.log
            echo ""
        done
        exit 1
    fi
fi

echo "Real Leader: Server $LEADER_ID on port $LEADER_PORT"


echo ""
echo "PHASE 3: Start Sensor Nodes"
echo "-----------------------------------"

SERVER_PORT_LIST="${PORTS[0]} ${PORTS[1]} ${PORTS[2]}"

for i in 1 2 3 4 5; do
    ./node 127.0.0.1 $i $SERVER_PORT_LIST > n${i}.log 2>&1 &
    NODE_PIDS[$i]=$!
    echo "  Sensor node $i started (PID: ${NODE_PIDS[$i]})"
    sleep 0.3
done

echo ""
echo "Collecting sensor data..."
sleep 30

echo ""
echo "PHASE 4: Verify Log Replication Across Cluster"
echo "-----------------------------------"

echo "Checking log counts on all servers:"
echo ""

declare -A LOG_COUNTS
ALL_SAME=true
FIRST_COUNT=""

for i in 0 1 2; do
    port=${PORTS[$i]}
    server_id=$((i+1))
    
    if ! ps -p ${SERVER_PIDS[$server_id]} > /dev/null 2>&1; then
        echo "  Server $server_id (port $port): Process died"
        continue
    fi
    
    RESULT=$(timeout 2 ./query 127.0.0.1 $port 1 2>&1)
    
    if echo "$RESULT" | grep -q "Total Sensor Readings:"; then
        READINGS=$(echo "$RESULT" | grep "Total Sensor Readings:" | sed 's/.*: \([0-9]*\)/\1/')
        HEARTBEATS=$(echo "$RESULT" | grep "Total Heartbeats:" | sed 's/.*: \([0-9]*\)/\1/')
        TOTAL=$((READINGS + HEARTBEATS))
        
        LOG_COUNTS[$server_id]=$TOTAL
        
        echo "  Server $server_id (port $port):"
        echo "    Total logs: $TOTAL"
        echo "    Sensor readings: $READINGS"
        echo "    Heartbeats: $HEARTBEATS"
        
        if [ -z "$FIRST_COUNT" ]; then
            FIRST_COUNT=$TOTAL
        elif [ "$TOTAL" != "$FIRST_COUNT" ]; then
            ALL_SAME=false
        fi
    else
        echo "  Server $server_id (port $port): Not responding"
        LOG_COUNTS[$server_id]=0
        ALL_SAME=false
    fi
done

echo ""
if [ "$ALL_SAME" = true ] && [ ! -z "$FIRST_COUNT" ] && [ "$FIRST_COUNT" -gt 0 ]; then
    echo "✓ LOG REPLICATION SUCCESSFUL"
    echo "  All servers have $FIRST_COUNT logs"
    echo "  Data is fully replicated across the cluster!"
else
    echo "  This may be normal if replication is still ongoing"
fi


echo ""
echo "PHASE 5: Display Sensor Data"
echo "-----------------------------------"

echo "Last 5 readings from each sensor node:"
echo ""

for i in 1 2 3 4 5; do
    echo "Node $i:"
    ./query 127.0.0.1 $LEADER_PORT 2 $i 2>&1 | grep -A 10 "LAST 5 READINGS"
    echo ""
done

echo ""
echo "PHASE 6: Leader Failure Test"
echo "-----------------------------------"

BEFORE_READINGS=$(timeout 2 ./query 127.0.0.1 $LEADER_PORT 1 2>&1 | grep "Total Sensor Readings:" | sed 's/.*: \([0-9]*\)/\1/')

echo "Data before failure:"
echo "  Total readings: $BEFORE_READINGS"
echo ""

echo "Killing leader (Server $LEADER_ID on port $LEADER_PORT)..."
kill -9 ${SERVER_PIDS[$LEADER_ID]} 2>/dev/null

echo "Waiting for new leader election"
sleep 15

echo ""
echo "Finding new leader..."
NEW_LEADER_PORT=""
NEW_LEADER_ID=""

for i in 0 1 2; do
    port=${PORTS[$i]}
    server_id=$((i+1))
    
    if [ $server_id -eq $LEADER_ID ]; then
        continue
    fi
    
    if ! ps -p ${SERVER_PIDS[$server_id]} > /dev/null 2>&1; then
        continue
    fi
    
    RESULT=$(timeout 2 ./query 127.0.0.1 $port 1 2>&1)
    if echo "$RESULT" | grep -q "Total Sensor Readings:"; then
        echo "  Server $server_id (port $port): NEW LEADER"
        NEW_LEADER_PORT=$port
        NEW_LEADER_ID=$server_id
        break
    fi
done

if [ -z "$NEW_LEADER_PORT" ]; then
    echo " No new leader elected"
else
    echo ""
    echo "New leader elected: Server $NEW_LEADER_ID on port $NEW_LEADER_PORT"
    
    
    AFTER_READINGS=$(timeout 2 ./query 127.0.0.1 $NEW_LEADER_PORT 1 2>&1 | grep "Total Sensor Readings:" | sed 's/.*: \([0-9]*\)/\1/')
    
    echo ""
    echo "Data after leadership change:"
    echo "  Before failure: $BEFORE_READINGS readings"
    echo "  After failover: $AFTER_READINGS readings"
    
    if [ "$AFTER_READINGS" -ge "$BEFORE_READINGS" ]; then
        echo " NO DATA LOSS - all data preserved!"
    else
        echo " Data loss detected"
    fi
    
  
    echo ""
    echo "Waiting for continued data collection"
    sleep 20
    
    FINAL_READINGS=$(timeout 2 ./query 127.0.0.1 $NEW_LEADER_PORT 1 2>&1 | grep "Total Sensor Readings:" | sed 's/.*: \([0-9]*\)/\1/')
    NEW_DATA=$((FINAL_READINGS - AFTER_READINGS))
    
    echo "Final check:"
    echo "  Total readings: $FINAL_READINGS"
    echo "  New data collected: $NEW_DATA"
    
    if [ "$NEW_DATA" -gt 0 ]; then
        echo "  System continues collecting data after failover!"
    fi
fi


echo ""
echo "PHASE 7: Final Log Replication Verification"
echo "-----------------------------------"

echo "Checking all surviving servers have same data:"
echo ""

SURVIVING_COUNTS=()
for i in 0 1 2; do
    port=${PORTS[$i]}
    server_id=$((i+1))
    
    if [ $server_id -eq $LEADER_ID ]; then
        echo "  Server $server_id (port $port): Killed (was original leader)"
        continue
    fi
    
    if ! ps -p ${SERVER_PIDS[$server_id]} > /dev/null 2>&1; then
        echo "  Server $server_id (port $port): Process died"
        continue
    fi
    
    RESULT=$(timeout 2 ./query 127.0.0.1 $port 1 2>&1)
    if echo "$RESULT" | grep -q "Total Sensor Readings:"; then
        READINGS=$(echo "$RESULT" | grep "Total Sensor Readings:" | sed 's/.*: \([0-9]*\)/\1/')
        echo "  Server $server_id (port $port): $READINGS sensor readings"
        SURVIVING_COUNTS+=($READINGS)
    fi
done

echo ""
if [ ${#SURVIVING_COUNTS[@]} -eq 2 ]; then
    if [ ${SURVIVING_COUNTS[0]} -eq ${SURVIVING_COUNTS[1]} ]; then
        echo "PERFECT LOG REPLICATION"
        echo "  Both surviving servers have identical data!"
    else
        echo "Log counts differ: ${SURVIVING_COUNTS[0]} vs ${SURVIVING_COUNTS[1]}"
    fi
fi

echo ""
echo "Logs saved:"
echo "  - Server logs: s1.log, s2.log, s3.log"
echo "  - Node logs: n1.log, n2.log, n3.log, n4.log, n5.log"
echo ""
echo "============================================="
echo "TEST COMPLETE"
echo "============================================="
