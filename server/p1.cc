#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <vector>
#include <sstream>
#include <pthread.h>
#include <signal.h>
#include <mutex>

using namespace std;

// global mutex
mutex g_mutex;

// global work queue
vector <vector<int>> g_queue;

class Server {
    public:
        Server();
        void schedule(vector<int>);
        void listen(int port);
        void process();
        void handle(int);
        void say(int, string);
        bool running();
    protected:
        int listen_socket;
        pthread_t listen_thread;
        vector<pthread_t> threads;
        vector<int> clients;
        bool active;
        static void *accept(void *);
        static void *io(void *);
};

// Creates a pool of threads which each execute the Server::io method.
Server::Server() {
    int threadPoolSize = 3;
    active = true;
    for (int i = 0; i != threadPoolSize; i++) {
        pthread_t thread;
        pthread_create(&thread, NULL, io, this);
        threads.push_back(thread);
    }
}

// Threadsafe method that adds an array of sockets to the work queue.
void Server::schedule(vector<int> clients) {
    g_mutex.lock();
    g_queue.push_back(clients);
    g_mutex.unlock();
}

// Creates a socket, binds to a port, listens for new connections and accepts
// them in Server::accpet as a seperate thread function.
void Server::listen(int port) {
    struct sockaddr_in sin;

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    listen_socket = socket(AF_INET, SOCK_STREAM, 0);

    bind(listen_socket, (struct sockaddr *) &sin, sizeof(sin));

    ::listen(listen_socket, 5);

    pthread_create(&listen_thread, NULL, accept, this);
}

// Groups up to 1024 client sockets into an array and schedules them for io.
void Server::process() {
    vector<int> work;

    for (vector<int>::size_type i = 0; i != clients.size(); i++) {
        if (work.size() >= 1024) {
            schedule(work);
            work.clear();
        }
        work.push_back(clients[i]);
    }

    if (work.size() != 0) {
        schedule(work);
    }
    
    // wait until all work is done
    while (active) {
        g_mutex.lock();
        bool done = g_queue.size() == 0;
        g_mutex.unlock();
        if (done) {
            return;
        }
    }
}

// Reads data from client and resends it to all other connected clients
void Server::handle(int client) {
    char buffer[1024] = { 0 };

    int bytesReceived = recv(client, buffer, sizeof(buffer), 0);

    memset(buffer + bytesReceived, 0, 1024 - bytesReceived);
    
    cout << buffer << endl;

    say(client, string(buffer));
}

// Sends message to all clients except the client specified.
void Server::say(int client, string message) {
    for (vector<int>::size_type i = 0; i != clients.size(); i++) {
        if (clients[i] != client) {
            ::send(clients[i], message.c_str(), message.length(), 0);
        }
    }
}

// Returns whether or not the server is still running.
bool Server::running() {
    return active;
}

// Thread function to accept new clients and put them into nonblocking mode.
void * Server::accept(void *self) {
    struct sockaddr_in sin;
    socklen_t sin_l = sizeof(sin);

    while (((Server *) self)->active) {
        int client_socket = ::accept(((Server *) self)->listen_socket, (struct sockaddr *) &sin, &sin_l);

        int flags = fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);

        g_mutex.lock();
        ((Server *) self)->clients.push_back(client_socket);        
        g_mutex.unlock();
    }
}

// Thread function to perform io on clients.
void * Server::io(void *self) {
    vector<int> work;

    while (((Server *) self)->active) {
        int working = false;
        g_mutex.lock();
        if (g_queue.size() > 0) {
            work = g_queue[0];
            g_queue.erase(g_queue.begin());
            working = true;
        }
        g_mutex.unlock();
        
        if (!working) continue;

        int flags;
        struct sockaddr_in sin;
        socklen_t sin_l = sizeof(sin);
        struct timeval td = {0, 1};
        fd_set fdread;
        int maxfd = 0;
        
        FD_ZERO(&fdread);
    
        for (vector<int>::size_type i = 0; i != work.size(); i++) {
            FD_SET(work[i], &fdread);
            if (work[i] > maxfd) {
                maxfd = work[i];
            }
        }
    
        select(maxfd + 1, &fdread, NULL, NULL, &td);
    
        for (vector<int>::size_type i = 0; i != work.size(); i++) {

            if (FD_ISSET(work[i], &fdread)) {
                ((Server *) self)->handle(work[i]);
                FD_CLR(work[i], &fdread);
            }
    
        }

    }
}

// Creates server and starts processing clients.
int main() {
    Server server;
    server.listen(6667);
    while (server.running()) {
        server.process();
    }
    return 0;
}
