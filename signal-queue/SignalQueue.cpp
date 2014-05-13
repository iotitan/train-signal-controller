/*
 * File: SignalQueue.cpp
 * Author: Matt Jones
 * Date: 5/4/2014
 * Desc: A simple worker-thread based message queue. All clients should
 *       remain connected as long as possible.
 * Protocol:
 *       Since there are a grand total of 8 possible things the signal
 *       can do, the message format will be simple:
 *       - One byte followed immediately by a null terminator ('\0')
 *       - Signal will operate based on which bits are set in the above byte
 *           - [0] blink flag
 *           - [1] red flag
 *           - [2] yellow flag
 *           - [3] green flag
 *           - [4] turn lamp on
 *           - [5] turn lamp off
 *           - [6] unused
 *           - [7] unused
 */


#include <queue>
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <pthread.h>


using namespace std;


#define MAX_THREADS 3
#define MAX_QUEUE 50
#define SERVER_PORT "19100"
#define SERVER_BACKLOG 4


/**
 * ThreadInfo struct for managing threads
 */
typedef struct threadInfo {
    int workerId;   // ID of the thread
    bool connected; // If the worker is actuall in use
    int socketFd;   // The ID of the socket to write to
    pthread_mutex_t threadMutex; // used to stop and start particular a thread (will block via spinlock or sleeping)
} ThreadInfo;

bool QUIT_FLAG;
//queue<char*> signalQueue;


// prototypes
void* handleClient(void* param);
int findAvailableWorker(ThreadInfo* workers, const int noWorkers);


/**
 * Main method used to manage worker threads
 */
int main(int argc, char* argv[]) {

    int i;
    QUIT_FLAG = false;

    // thread vars
    ThreadInfo* threadInfoList = new ThreadInfo[MAX_THREADS];
    pthread_t* threadIds = new pthread_t[MAX_THREADS];
    
    // initialize thread information & start threads
    for(i = 0; i < MAX_THREADS; i++) {
        threadInfoList[i].workerId = i;
        threadInfoList[i].connected = false;
        
        pthread_mutex_init(&threadInfoList[i].threadMutex,NULL);
        pthread_mutex_lock(&threadInfoList[i].threadMutex);

        // using NULL for pthread_attr_t (just use default behavior)
        pthread_create(&threadIds[i], NULL, handleClient, &threadInfoList[i]);
    }

    // socket vars
    struct sockaddr_storage clientAddr;
    socklen_t addrSize;
    struct addrinfo hints, *res;
    int sockFd, clientFd;

    // init socket info
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    getaddrinfo(NULL, SERVER_PORT, &hints, &res);
    sockFd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    if(sockFd < 0) {
        cout << "Failed to create listening socket \n" << strerror(errno);
        exit(1);
    }

    bind(sockFd, res->ai_addr, res->ai_addrlen);
    listen(sockFd, SERVER_BACKLOG);

    addrSize = sizeof(clientAddr);

    // main loop waits for a new connection and assigns to a thread if one is available
    while(!QUIT_FLAG) {
        clientFd = accept(sockFd, (struct sockaddr *)&clientAddr, &addrSize);

        if(clientFd < 0) {
            cout << "Client did not connect successfully \n";
        }
        else {
            cout << "A client connected! \n";
        }

        // find available worker and assign
        int readyWorker = findAvailableWorker(threadInfoList, MAX_THREADS);
        if(readyWorker < 0) {
            // if we failed, send error message
            string errorMsg = "{\"error\":true,\"code\":1,\"message\":\"No available connections.\"}";
            //string errorMsg = "e:1";
            send(clientFd, errorMsg.c_str(), errorMsg.length()+1, 0);
            close(clientFd);
        }
        else {
            // if we succeeded, send initiation message
            string welcomeMsg = "{\"error\":false}";
            //string errorMsg = "e:0";
            send(clientFd, welcomeMsg.c_str(), welcomeMsg.length()+1, 0);
            
            // set a thread as connected and unlock mutex for that thread
            threadInfoList[readyWorker].socketFd = clientFd;
            threadInfoList[readyWorker].connected = true;

            // finally unlock mutex so the thread starts going again
            pthread_mutex_unlock(&threadInfoList[readyWorker].threadMutex);
        }
    }

    // join threads
    for(i = 0; i < MAX_THREADS; i++) {
        pthread_join(threadIds[i],NULL);
    }

    // clean up
    delete[] threadInfoList;
    delete[] threadIds;

    cout << "Server exiting...\n";

    return 0;
}



/**
 * Client handler that each thread will run
 */
void* handleClient(void* param) {

    // keep handle to thread info (this will be changed by master thread)
    ThreadInfo* tInfo = (ThreadInfo*)param;   

    int myId = tInfo->workerId;
 
    // perpetually try to work
    while(!QUIT_FLAG) {

        // wait for master to unlock this thread
        pthread_mutex_lock(&tInfo->threadMutex);
    cout << "Thread Info:\n";
    cout << tInfo->workerId << "\n";
    cout << tInfo->connected << "\n";
    cout << tInfo->socketFd << "\n";


        cout << "Client connected to thread " << myId << "\n";
        int clientSoc = tInfo->socketFd;

        if(tInfo->connected) {

            // thread mutex will be unlocked at this point, immediately relock this thread's mutex once we 
            // are past the connected condition so we don't automatically start again once this client disconnects
       //     pthread_mutex_lock(&(tInfo->threadMutex));

    cout << "OK\n";
            // is the client going to be a reader or a writer?
            char* cType = new char[10];
            int size = recv(clientSoc, cType, 1, 0);
    cout << size;
    cout << cType[0] << "\n";
/*            if(cType == 'R' || cType == 'r') {
    cout << "im a reader";
                // client is a reader 
                while(!signalQueue.empty() || size < 1) {
                    if(signalQueue.size() < 50) {
                        char* sig = new char[3];
                        size = send(threadInfoList[myId].socketFd, sig, 3, 0);
                        cout << sig;
                    }
                    
                    if(size < 1) {
                        cout << "Client disconnected!\n";
                    }
                }
            }
            else*/ if(cType[0] == 'W' || cType[0] == 'w') {
                // client is a writer
    cout << "im a writer";
                while(size > 0) {
                    char* sig = new char[10];
                    size = recv(clientSoc, sig, 3, 0);
                    cout << sig;
                }
            }

    cout << "closing socket";
            //stringstream msgStream;
            //msgStream << "hello from thread " << myId;
            //string msg = msgStream.str();

            close(clientSoc);
        }
        
        tInfo->connected = false;
    }

    cout << "Exiting thread " << myId << "\n";
}


/**
 * Find a worker thread that is not busy. If none are available, return -1.
 * @param workers A pointer to the array of worker information
 * @param noWorkers The number of workers available
 * @return The index of the next available worker or -1 if there are none
 */
int findAvailableWorker(ThreadInfo* workers, const int noWorkers) {
    if(workers == NULL) {
        return -1;
    }

    int i;
    for(i = 0; i < noWorkers; i++) {
        if(!workers[i].connected) {
            return i;
        }
    }

    return -1;
}

