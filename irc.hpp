#pragma once
#include <cstdio>

#include <iostream>
#include <map>
#include <vector>
#include <sstream>
#include <cstring>
#include <algorithm>

#include <sys/types.h>
#include <sys/socket.h>   // send, recv, socket, bind, listen, accept
#include <netinet/in.h>   // sockaddr_in, htons
#include <arpa/inet.h>    // inet_ntoa, etc.
#include <unistd.h>       // close
#include <fcntl.h>        // fcntl (for non-blocking sockets)
#include <poll.h>         // poll, pollfd

class Client;
class Channel;
class Server;

#include "parser.hpp"
#include  "error.hpp"


#include "client.hpp"
#include "channel.hpp"
#include  "server.hpp"
