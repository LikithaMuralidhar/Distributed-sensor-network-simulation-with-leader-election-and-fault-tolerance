# Distributed Sensor Network Simulation with Leader Election and Fault Tolerance

## Project Overview
This project simulates a distributed embedded sensor network entirely in software.  
Each sensor node runs as an independent process and communicates with other nodes and servers using TCP sockets. A base station (leader) collects sensor readings, and the system uses the Raft consensus algorithm to handle leader failures and ensure fault tolerance.

The project demonstrates core distributed systems concepts such as leader election, heartbeat-based failure detection, log replication, and state-machine consistency.

## Key Features
- Software-based simulation of embedded sensor nodes
- TCP socket-based inter-node communication
- Periodic sensor data generation (temperature, humidity)
- Central base station for data collection
- Raft-based leader election and failover
- Heartbeat mechanism for failure detection
- Log replication and consistency verification
- Automated and manual testing support

## System Architecture
- **Sensor Nodes**
  - Generate random sensor readings periodically
  - Send data to the current leader (base station)
  - Participate in heartbeat monitoring

- **Base Station (Leader)**
  - Collects and aggregates sensor readings
  - Maintains replicated logs
  - Coordinates cluster operations

- **Raft Servers**
  - Maintain replicated state machines
  - Participate in leader election
  - Ensure consistency after failures

## Communication Model
- TCP sockets for all inter-process communication
- Dedicated channels for:
  - Sensor data transmission
  - Heartbeat messages
  - Leader election coordination

## Leader Election
- Implemented using the Raft consensus algorithm
- Nodes detect leader failure via heartbeat timeouts
- New leader is elected automatically
- System continues operation without data loss

## Assumptions
- All sensor nodes are homogeneous
- Network communication is reliable within the simulation
- Single configurable cluster of nodes
- Leader failures occur randomly and are detectable

## Project Modules
- **Node Module**
  - Sensor data generation
  - Data transmission
  - Heartbeat send/receive logic

- **Base Station Module**
  - Sensor data aggregation
  - Node status tracking
  - Failure detection

- **Leader Election Module**
  - Raft election logic
  - Log replication
  - State machine management

- **Communication Module**
  - Socket handling
  - Message parsing
  - Reliable data exchange

- **Query Module**
  - Inspect latest sensor readings
  - Verify log consistency across servers

## Evaluation and Testing
The system was evaluated using:
- Manual testing for leader behavior and failover observation
- Automated end-to-end testing using a script

### Evaluation Scenarios
1. Log replication before leader failure
2. Controlled leader crash and recovery
3. End-to-end sensor data ingestion
4. Log consistency after leader change

Results confirmed:
- Correct leader election
- No data loss during failover
- Identical logs across surviving servers

## How to Run

### Option 1: Automated Test
```bash
./test.sh
