# RemoteOps – Network Programming

## Student Information

- Registration Number: IT24102219
- Module: IE3090 Network Programming
- Project: RemoteOps Agent and Controller
- Platform: CentOS Linux
- Language: C

## Project Description

RemoteOps is a TCP-based remote system management application developed using C socket programming. The system consists of a RemoteOps Agent and a Controller. The Controller connects to the Agent and sends commands, while the Agent processes the requests and returns responses.

## Main Features

- TCP client-server communication
- Authentication using a personalised token
- Session ID management
- SYSINFO system monitoring
- LISTPROC process listing
- Whitelisted EXEC commands
- PUT file upload
- GET file download
- UDP-based system monitoring
- Multiple Controller connections using pthreads
- Event logging
- Graceful connection handling

## Personalised Values

- TCP Port: 9410
- Authentication Token: OPS-2219
- Session ID: 9122

## Source Files

- `agent_219.c` – RemoteOps Agent
- `controller_219.c` – RemoteOps Controller
- `Makefile_219` – Build configuration

## Compilation

Compile the Agent:

```bash
gcc -Wall -Wextra -pthread -o agent_test agent_219.c
