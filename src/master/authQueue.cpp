#ifndef LODEAQUEUE_H
#define LODEAQUEUE_H
#include <list>
#include <chrono>
#include <string>
#include <thread>
#include <future>
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
            }

            AuthQueue(AuthQueue&& moved){
                authenticatedList = moved.authenticatedList;
                password = std::move(moved.password);
            }

            void setPass(std::string pass){
                passLock.lock();
                password = pass;
                passLock.unlock();
            }

            void insertNode(autheableNode newNode){
                listLock.lock();
                list.push_back(newNode);
                listLock.unlock();
            }

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
            void authorizeNodes(std::chrono::milliseconds timeout){
                std::list<autheableNode>::iterator it;
                
                //check for deletion thread signals
                // NOTE: This should maybe be extracted since looping and sleeping
                // is already done at the caller's discretion
                bool result = awaitSignal.try_wait();
                if(result){
                    waitingSignal.post(); //notify deletion thread that this thread is waiting
                    continueSignal.wait(); //wait until deletion thread signals to continue
                }
                
                
                //loop auth queue and try to authenticate each one respecting timeout
                for(it = list.begin(); it != list.end(); ++it){
                    if(!it->active || !it->lock.try_lock())
                        continue;

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

            void authorizeNodes(int timeInMillis){
                authorizeNodes(std::chrono::milliseconds(timeInMillis));
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
             * Deletes inactive authQueue entries once a specific cutoff is met.
             *
             * Will call cleanList with the corresponding authentication semaphores
             * once cutoff is met, locking [queue]
             *
             * DOES NOT LOOP BY ITSELF; sleeping and looping is done at the caller's
             * discretion.
             *
             * @param cutoff the amount of inactive items after which they are delted.
             * */
            void cleanAuthQueue(unsigned int cutoff){
                //count amount of inactive queue entries
                int count = 0;
                std::list<autheableNode>::iterator it;
                for(it = list.begin(); count < cutoff && it != list.end(); it++){
                    if(!it->active)
                        count++;
                }
                
                //notify auth threads to wait, block until they are all waiting,
                //delete inactive entries then notify threads deletion is done
                if(count >= cutoff){
                    listLock.lock();
                    cleanList();
                    listLock.unlock();
                }
            }

        private:
            std::string password = " ";
            std::mutex passLock;
            std::list<connectedNode>* authenticatedList;

    };
}
#endif
