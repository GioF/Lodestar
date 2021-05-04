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

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{

    /**
     * Class responsible for implementing service discovery for each publisher/subscriber.
     * */
    class Master {
        friend class Master_test;

        public:

            /**
             * A struct that represents nodes which register to topics.
             * */
            struct registrar {
                std::string address;  ///< string used by the node to identify an instance of a publisher/subscriber.*/
                int nodeSocketFd;     ///< the socket file descriptor of the node.*/
            };
            
            /**
             * A struct that defined a node of the topic tree.
             *
             * Node, in this context, is used to refer to an element in a tree,
             * instead of a component in a distributed system.
             * */
            struct topicTreeNode {
                nodeType type;                       ///< The type of the tree node; a directory of topics or a topic.
                std::string name;                    ///< The name of the topic or directory.
                std::vector<topicTreeNode> subNodes; ///< Subdirectories of a directory; empty if a topic.
                std::vector<registrar> publishers;   ///< a vector of nodes that publish to this topic; empty if a directory.
                std::vector<registrar> subscribers;  ///< a vector of nodes that subscribe to this topic; empty if a directory.
            };
            
            /**
             * A struct that represents a reference to a topic, from the node array.
             * */
            struct topicTreeRef {
                std::string address;         ///< the same as the registrar address.
                topicTreeNode* topicPointer; ///< a pointer to the subscriber topic.
                registrar* directPointer;    ///< a direct pointer to the registrar.
            };
            
            /**
             * A struct that represents a node.
             *
             * This is the common meaning of a node; a component in a distributed
             * system. 
             * */
            struct node {
                int socketFd;                          ///< The file descriptor of the nodes' socket.
                std::vector<topicTreeRef> publishers;  ///< vector of topics the node publishes to.
                std::vector<topicTreeRef> subscribers; ///< vector of topics the node subscribes to.
            };

            /**
             * A struct that represents an authenticable socket.
             *
             * An authenticable socket is a socket which is connected, but is yet to
             * send an identifier or a session ID to be treated as a node which is authorized
             * to communicate with the Master.
             * */
            struct autheableNode {
                std::mutex lock;
                message authmsg; ///< where the auth message will be
                std::chrono::time_point<std::chrono::steady_clock> timeout; ///< when it will timeout
                int sockfd = 0;      ///< the file descriptor of the authenticable socket
                bool active = true;

                autheableNode(const autheableNode& node){
                    authmsg = node.authmsg;
                    timeout = node.timeout;
                    sockfd = node.sockfd;
                    active = node.active;
                }

                autheableNode(){}
            };

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
            Master(std::string sockPath):authAwaitSignal(0), authWaitingSignal(0), authContinueSignal(0){
                setupListener(sockPath);
            }

            /**
             * Constructor which starts the listener with default values conditionally.
             *
             * @param startNode boolean that informs constructor if listener should be started.
             * */
            Master(bool startListener = false): authAwaitSignal(0), authWaitingSignal(0), authContinueSignal(0){
                // TODO: function should also read config files for default
                // socket path and call Master(std::string sockpath)
                // with said path, with configurable timeout and number of threads
                if(startListener){
                    std::string socketPath = std::string(getenv("HOME"));
                    socketPath.append("/.local/share/lodestar/mastersocket");
                    setupListener(socketPath);
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
            std::string password = "";           ///< password that is authenticated against
            std::list<autheableNode> authQueue;  ///< queue of sockets awaiting authentication
            std::chrono::seconds gracePeriod;    ///< time after which nodes are disconnected if unauthenticated
            std::mutex queueLock;
            int nThreads = 0;                    ///< amount of running threads
            semaphore authAwaitSignal;           ///< sent from deletion thread to auth threads to notify them to wait
            semaphore authWaitingSignal;         ///< sent from auth thread to deletion thread to notify they are waiting
            semaphore authContinueSignal;        ///< sent from deletion thread to auth threads to notify deletion is done
            
            topicTreeNode* rootNode = new topicTreeNode; ///< tree of directories and topics.
            std::vector<node> nodeArray;                 ///< array of nodes connected to this master.

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

                        queueLock.lock();
                        authQueue.push_back(newNode);
                        queueLock.unlock();
                    }
                    if(rv < 0){
                        // TODO: proper error logging
                        std::cout << "error while polling: " << rv;
                    }
                }
            };

            /**
             * Queue authentication function.
             *
             * Will loop through the queue calling the recvMessage_for function of the
             * item's message object with [timeout] timeout and mark the entries that
             * exceed their object's respective timeout as inactive so that the queue
             * deletion thread can clean them up.
             *
             * Every start of the main loop, it checks if the queue deletion thread has
             * sent a signal through authAwaitSignal; if so, notifies the deletion thread
             * that it is currently waiting for the deletion to be complete and then waits
             * for the signal that it is complete.
             *
             * DOES NOT LOOP BY ITSELF; sleeping and looping is done at the caller's
             * discretion.
             *
             * @param queue the authenticable node queue.
             * @param timeout the time to be spent receiving each message.
             * */
            void authorizeNodes(std::list<autheableNode>* queue, std::chrono::milliseconds timeout){
                std::list<autheableNode>::iterator it;
                
                //check for deletion thread signals
                // NOTE: This should maybe be extracted since looping and sleeping
                // is already done at the caller's discretion
                bool result = authAwaitSignal.try_wait();
                if(result){
                    authWaitingSignal.post(); //notify deletion thread that this thread is waiting
                    authContinueSignal.wait(); //wait until deletion thread signals to continue
                }
                
                
                //loop auth queue and try to authenticate each one respecting timeout
                for(it = queue->begin(); it != queue->end(); ++it){
                    if(!it->active || !it->lock.try_lock())
                        continue;
                    msgStatus status = it->authmsg.recvMessage_for(it->sockfd, timeout);
                    
                    switch(status){
                        //if still receiving or not receiving at all
                        case msgStatus::receiving:
                        case msgStatus::nomsg:{
                            if(it->timeout > std::chrono::steady_clock::now())
                                break;
                            else{
                                it->active = false; //mark for deletion if timeout has passed
                                break;
                            }
                        }
                            //if just received
                        case msgStatus::ok:{
                            it->authmsg.deserializeMessage();
                            bool granted = authenticate(static_cast<auth*>(it->authmsg.data));
                            if(granted){
                                node newNode;
                                newNode.socketFd = it->sockfd;
                                nodeArray.push_back(newNode);
                            }
                            it->active = false;
                            break;
                        }
                    }
                    
                    it->lock.unlock();
                }
            };

            /**
             * Authenticates a node.
             *
             * For now just a placeholder until i finish implementing the core authentication
             * pipeline.
             *
             * @param node the auth message sent by the node.
             * @returns true if the node was granted permission, false if not.
             * */
            bool authenticate(auth* node){
                if(node->identifier == password)
                    return true;
                else return false;
            }

            /**
             * Deletes inactive queue entries once a specific cutoff is met.
             *
             * Will send [nThreads] signals via authAwaitSignal so that authentication
             * threads are notified to wait, then wait for them to notify they are
             * waiting; once it's done, deletes inactive entries and sends [nThreads]
             * signals via authContinueSignal to notify threads deletion is done.
             * The queueLock is also locked, so that sudden insertions can't result in
             * undefined behaviour.
             *
             * DOES NOT LOOP BY ITSELF; sleeping and looping is done at the caller's
             * discretion.
             *
             * @param queue que queue to be cleaned up.
             * @param cutoff the amount of inactive items after which they are delted.
             * */
            void cleanupQueue(std::list<autheableNode>* queue, unsigned int cutoff){
                //count amount of inactive queue entries
                int count = 0;
                std::list<autheableNode>::iterator it;
                for(it = queue->begin(); count < cutoff && it != queue->end(); it++){
                    if(!it->active)
                        count++;
                }

                //notify auth threads to wait, block until they are all waiting,
                //delete inactive entries then notify threads deletion is done
                if(count >= cutoff){
                    queueLock.lock();
                    for(int n = 0; n < nThreads; n++)
                        authAwaitSignal.post();

                    for(int n = 0; n < nThreads; n++)
                        authWaitingSignal.wait();

                    queue->remove_if([](autheableNode item){return !item.active;});

                    for(int n = 0; n < nThreads; n++)
                        authContinueSignal.post();
                    queueLock.unlock();
                }
            }
    };
}

#endif
