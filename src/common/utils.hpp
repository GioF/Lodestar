#ifndef LODEUTIL_H
#define LODEUTIL_H
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/**
 * @file utils.hpp
 * General utility functions.
 * */

/**
 * Creates a socket, binds it to [socketPath] and returns its file descriptor.
 *
 * @param socketPath where the socket is to be bound.
 * @param[out] sockaddr the resulting sockaddr.
 * 
 * @returns socket bound to [socketPath].
 * */
int createBoundSocket(std::string socketPath, sockaddr_un* sockaddr){
    socklen_t addrlen = sizeof(struct sockaddr_un);
    int sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if(sockfd < 0)
        throw "Error creating socket";
    
    sockaddr->sun_family = AF_LOCAL;
    std::strcpy(sockaddr->sun_path, socketPath.c_str());

    if(bind(sockfd, (struct sockaddr *) sockaddr, addrlen)){
        if(errno == EADDRINUSE){
            unlink(sockaddr->sun_path);
            if(bind(sockfd, (struct sockaddr *) sockaddr, addrlen)){
                throw errno;
            }
        }else throw errno;
    };

    return sockfd;
}

/**
 * Accepts one connection to [listeningSocket] and returns the file descriptor.
 *
 * Sets socket to listen with a backlog of 1, so it isn't necessary to use
 * listen() on [listeningSocket] before passing it to this function.
 *
 * @param listeningSocket the socket that will listen to connections.
 * @param sockaddr unix socket address.
 * */
int acceptOne(int listeningSocket, sockaddr_un* sockaddr){
    socklen_t addrlen = sizeof(struct sockaddr_un);
    listen(listeningSocket, 1);
    return accept(listeningSocket, (struct sockaddr*)sockaddr, &addrlen);
}

#endif
