#ifndef LODEMST_H
#define LODEMST_H

#include <vector>
#include <list>
#include <chrono>
#include <string>
#include <cstring>
#include <mutex>
#include <thread>
#include <iostream>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include "../common/communication.cpp"
#include "../common/types.h"
#include "authQueue.cpp"
#include "types.hpp"

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{

    /**
     * Class responsible for implementing service discovery for each publisher/subscriber.
     * */
    class Master {
        friend class Master_test;

        public:
            ~Master(){
                if(listeningThread && listeningThread->joinable()){
                    isOk = false;
                    listeningThread->join();
                }
                close(sockfd);
                unlink(sockaddr.sun_path);
            }

            /**
             * Constructor which also sets up the listener.
             *
             * @param sockPath the desired path to be used with the socket
             * */
            Master(std::string sockPath){
                setupListener(sockPath);
                gracePeriod = std::chrono::seconds(20);
            }

            /**
             * Constructor which starts the listener with default values conditionally.
             *
             * @param startNode boolean that informs constructor if listener should be started.
             * */
            Master(bool startListener = false){
                // TODO: function should also read config files for default
                // socket path and call Master(std::string sockpath)
                // with said path, with configurable timeout and number of threads
                gracePeriod = std::chrono::seconds(20);
                if(startListener){
                    std::string socketPath = std::string(getenv("HOME"));
                    socketPath.append("/.local/share/lodestar/mastersocket");
                    setupListener(socketPath);

                    // NOTE: remember to call this based on config
                    authQueue = std::move(AuthQueue(nodeArray, " ", 10, 3, 200ms));
                }
            };

            /**
             * Starts the listener thread using sockPath as the path.
             *
             * @param sockPath the desired path to be used with the socket
             * */
            void setupListener(std::string sockPath){
                sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
                if(sockfd < 0)
                    throw "Error creating socket";

                sockaddr.sun_family = AF_LOCAL;
                std::strcpy(sockaddr.sun_path, sockPath.c_str());

                if(bind(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr_un))){
                    throw "Error binding socket";
                };

                listen(sockfd, 10);
                listeningThread = new std::thread(&Master::listenForNodes, this, sockfd);
            };

        private:
            // TODO: tidy up following horribleness
            bool isOk = true;     ///< variable that tracks if class is ok (not shutting down)
            int sockfd;           ///< master listening socket file descriptor.
            sockaddr_un sockaddr;

            std::thread *listeningThread = NULL; ///< pointer to listener thread
            std::chrono::seconds gracePeriod;    ///< time after which nodes are disconnected if unauthenticated
            
            topicTreeNode* rootNode = new topicTreeNode; ///< tree of directories and topics.
            std::list<connectedNode> nodeArray;        ///< array of nodes connected to this master.
            AuthQueue authQueue = AuthQueue(nodeArray, " ", 5);
            
            /**
             * Tokenizes a path string with "/" as delimiter.
             *
             * @param[in] Path the path string to be tokenized.
             * @returns The vector in which each element is a "directory" of the path.
             * */
            std::vector<std::string> tokenizeTopicStr(std::string path){
                std::vector<std::string> separatedPath;
                char *token;
                char separator[] = "/";

                token = strtok(&path[0], separator);

                while(token != NULL){
                    separatedPath.push_back(std::string(token));
                    token = strtok(NULL, separator);
                }

                return separatedPath;
            }

            /**
             * Traverses the topic tree and returns directory at the end of a path.
             *
             * If during tree traversal a directory is not found, it will be created.
             *
             * @param[in] dirPath A vector that represents a path.
             * @returns A pointer to the topic tree node which has the last element dirPath as its name.
             * */
            topicTreeNode* getDir(std::vector<std::string> dirPath){
                topicTreeNode *currentDir = rootNode;
                topicTreeNode *foundDir = NULL;
                std::vector<topicTreeNode>::iterator subNodeIterator;

                for(int i = 0; i < dirPath.size(); i++){
                    //to avoid unnecessary processing in the case
                    //the next nodes are all empty
                    if(currentDir->subNodes.empty()){
                        currentDir->subNodes.push_back(topicTreeNode {nodeType::dir, dirPath[i]});
                        currentDir = &currentDir->subNodes.back();
                    } else {
                        for(subNodeIterator = currentDir->subNodes.begin(); subNodeIterator != currentDir->subNodes.end(); subNodeIterator++){
                            if(subNodeIterator->name == dirPath[i])
                                foundDir = &(*subNodeIterator);
                        }
                        
                        //if subnode with given name was not found, insert it
                        if(foundDir == NULL){
                            currentDir->subNodes.push_back(topicTreeNode {nodeType::dir, dirPath[i]});
                            currentDir = &currentDir->subNodes.back();
                        } else {
                            currentDir = foundDir;
                        }
                    }
                }

                return currentDir;
            }

            /**
             * Gets a topic from a directory.
             *
             * If a topic with the given does not exist, it will be created.
             *
             * @param[in] dir The directory in which the topic is (or will be)
             * @param[in] topicName The topic name.
             *
             * @returns A pointer to the topic.
             * */
            topicTreeNode* getTopic(topicTreeNode* dir, std::string topicName){
                topicTreeNode* topic = NULL;

                std::vector<topicTreeNode>::iterator it;
                for(it = dir->subNodes.begin(); it != dir->subNodes.end(); it++){
                    if(it->type == nodeType::topic && it->name == topicName)
                        topic = &(*it);
                }

                return topic;
            }

            // TODO: also insert topic into node on nodeArray
            /**
             * Registers a node to a topic.
             *
             * @param path The path of the topic.
             * @param registrarType The relation of the node to the topic ("pub": publication or "sub": subscription).
             * @param nodeSocket The socket file descriptor of the node.
             * @param address The address of the node.
             * */
            void registerToTopic(std::string path, std::string registrarType, int nodeSocket, std::string address){
                std::vector<std::string> tokenizedPath = tokenizeTopicStr(path);
                std::string topicName = tokenizedPath.back();
                tokenizedPath.pop_back();

                topicTreeNode* dir = getDir(tokenizedPath);
                topicTreeNode* topic = getTopic(dir, topicName);

                if(!topic){
                    dir->subNodes.push_back(topicTreeNode {nodeType::topic, topicName});
                    topic = &(dir->subNodes.back());
                }

                registrarType == "pub" ?
                    topic->publishers.push_back(registrar {address, nodeSocket}):
                    topic->subscribers.push_back(registrar {address, nodeSocket});
            }

            /**
             * Connection listener function.
             *
             * Will listen for connections on sockfd and when connected, the new file descriptor will
             * be sent to the authentication queue.
             * 
             * @param sockfd the listening socket.
             * */
            void listenForNodes(int sockfd){
                // polling could be used on the future to multiplex AF_UNIX and AF_INET sockets on this function
                sockaddr_un inSockaddr;
                int newSockfd = 0; ///< set newSockfd to a positive number for error checking
                int rv;
                socklen_t addrlen = sizeof(struct sockaddr_un);

                struct pollfd pfd;
                pfd.fd = sockfd;
                pfd.events = POLLIN;

                //simply accept all inbound connections and push them to authentication queue
                //(while socket exists)
                while(isOk && (newSockfd != EBADF || newSockfd != ENOTSOCK)){
                    rv = poll(&pfd, 1, (0.5 * 1000));

                    if(rv > 0){
                        newSockfd = accept(sockfd, (struct sockaddr *)&inSockaddr, &addrlen);
                        autheableNode newNode;
                        newNode.sockfd = newSockfd;
                        newNode.timeout = std::chrono::steady_clock::now() + gracePeriod;

                        authQueue.insertNode(newNode);
                    }
                    if(rv < 0){
                        // TODO: proper error logging
                        std::cout << "error while polling: " << rv;
                    }
                }
            };

    };
}

#endif
