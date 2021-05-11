#ifndef LODEAQUEUE_H
#define LODEAQUEUE_H
#include <list>
#include <chrono>
#include <string>
#include <thread>
#include <future>
#include <atomic>
#include <sys/socket.h>
#include <sys/un.h>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include "../common/managedList.cpp"
#include "../common/utils.hpp"
#include "../common/doctest.h"
#include "types.hpp"

namespace Lodestar{
    class AuthQueue: ManagedList<autheableNode>{
        public:
            AuthQueue(std::list<connectedNode>& connList){
                authenticatedList = &connList;
                cutoff = 5;
            }

            AuthQueue(std::list<connectedNode>& connList, int _cutoff){
                authenticatedList = &connList;
                cutoff = _cutoff;
            }

            AuthQueue(AuthQueue&& moved){
                authenticatedList = moved.authenticatedList;
                password = std::move(moved.password);
                cutoff = moved.cutoff;
            }

            using ManagedList::spin;

            void setPass(std::string pass){
                passLock.lock();
                password = pass;
                passLock.unlock();
            }

            int threadHeuristic(){
                return 0;
            }

            /**
             * Simply inserts an authenticable node into the base class' list.
             *
             * @param newNode the node to be inserted.
             * */
            void insertNode(autheableNode newNode){
                listLock.lock();
                list.push_back(newNode);
                listLock.unlock();
            }

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
                bool returnVal;
                std::string equivString(node->identifier);
                passLock.lock();
                returnVal = equivString == password;
                passLock.unlock();
                return returnVal;
            }
            
            /**
             * Heuristic based on the amount of inactive entries of list.
             *
             * @returns true if amount of inactive entries is greater than cutoff.
             * */
            bool deletionHeuristic(){
                int count = 0;
                std::list<autheableNode>::iterator it;
                for(it = list.begin(); count < cutoff && it != list.end(); it++){
                    if(!it->active)
                        count++;
                }
                
                return count >= cutoff;
            }

            /**
             * Manage function that authenticates nodes.
             *
             * Will loop through the queue calling the recvMessage_for function of the
             * item's message object with a timeout of [iteratorTimeout] milliseconds
             * and mark the entries that exceed their object's respective timeout
             * as inactive so that they can be cleaned up later.
             *
             * Every start of the main loop, it checks if the queue deletion thread has
             * sent a signal through authAwaitSignal; if so, notifies the deletion thread
             * that it is currently waiting for the deletion to be complete and then waits
             * for the signal that it is complete.
             * */
            void manage(){
                std::list<autheableNode>::iterator it;
                
                //loop auth queue and try to authenticate each one respecting timeout
                for(it = list.begin(); it != list.end(); ++it){
                    if(!it->active || !it->lock.try_lock())
                        continue;
                    auto timeout = std::chrono::milliseconds(iteratorTimeout);

                    msgStatus status;
                    try{
                        status = it->authmsg.recvMessage_for(it->sockfd, timeout);
                    }catch(int err){
                        //mark inactive it socket errors out
                        it->active = false;
                        it->lock.unlock();
                        continue;
                    }
                    
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
                                connectedNode newNode;
                                newNode.socketFd = it->sockfd;
                                authenticatedList->push_back(newNode);
                            }
                            it->active = false;
                            break;
                        }
                    }
                    
                    it->lock.unlock();
                }
            };

        private:
            int cutoff = 2;
            std::atomic<int> iteratorTimeout = 100;
            std::string password = " "; ///< the password this object authenticates each node against.
            std::mutex passLock;
            std::list<connectedNode>* authenticatedList; ///< a pointer to the authenticated node list.

            TEST_CASE_CLASS("AuthQueue - internal business logic"){
                std::list<connectedNode> connList;
                AuthQueue authQueue = AuthQueue(connList);
                
                Lodestar::autheableNode dummyEntry;
                dummyEntry.timeout = std::chrono::steady_clock::now();
                dummyEntry.sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
                dummyEntry.active = true;
                
                SUBCASE("general socket error - error treatment"){
                    close(dummyEntry.sockfd);
                    
                    authQueue.insertNode(dummyEntry);
                    authQueue.manage();

                    REQUIRE(!authQueue.list.front().active);
                }
                
                SUBCASE("default operation - entry treatment"){
                    sockaddr_un sockaddr;
                    int listeningSocket = createBoundSocket("/tmp/authTest.soc", &sockaddr);
                    auto futureSocket = std::async(std::launch::async, &acceptOne, listeningSocket, &sockaddr);
                    std::this_thread::sleep_for(std::chrono::milliseconds(400));
                    
                    //connect a dummy socket and put it on the queue
                    int dummySock = socket(AF_LOCAL, SOCK_STREAM, 0);
                    connect(dummySock, (struct sockaddr*) &sockaddr, sizeof(sockaddr_un));
                    int rxSock = futureSocket.get();

                    autheableNode dummyNode;
                    dummyNode.active = true;
                    dummyNode.sockfd = rxSock;
                    dummyNode.timeout = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                    authQueue.insertNode(dummyNode);
                    
                    //make socket timeout after blocking for 100 milliseconds
                    timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 100000;
                    setsockopt(authQueue.list.front().sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval));
                    
                    SUBCASE("message timed out"){
                        //set up queue entry to timeout immediately
                        authQueue.list.front().timeout = std::chrono::steady_clock::now();
                        
                        //try to authorize without sending nothing so it times out
                        authQueue.manage();
                        REQUIRE(!authQueue.list.front().active);
                    }
                    
                    SUBCASE("no message sent"){
                        //set up very long timeout so it doesn't timeout
                        auto entryTimeout = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                        authQueue.list.front().timeout = entryTimeout;
                        
                        authQueue.manage();
                        REQUIRE(authQueue.list.front().active);
                    }
                    
                    SUBCASE("unfinished message"){
                        //set up very long timeout so it doesn't timeout
                        auto entryTimeout = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                        authQueue.list.front().timeout = entryTimeout;
                        
                        //send size header and nothing more
                        int dummybuf = 10;
                        auto sent = send(dummySock, &dummybuf, 2, 0);
                        
                        authQueue.manage();
                        REQUIRE(authQueue.list.front().active);
                    }
                    
                    SUBCASE("fully received message"){
                        //set up dummy auth message
                        Lodestar::auth dummyAuth;
                        dummyAuth.size = 2;
                        char emptyIdentifier[] = " ";
                        dummyAuth.identifier = emptyIdentifier;
                        
                        //send it
                        Lodestar::message dummyMsg;
                        dummyMsg.data = &dummyAuth;
                        dummyMsg.sendMessage(dummySock);
                        
                        //authorize and check if it was correctly added
                        //to node list
                        authQueue.manage();
                        REQUIRE(!authQueue.list.front().active);
                        REQUIRE(connList.size() == 1);
                    }
                }
            };
    };
}
#endif
