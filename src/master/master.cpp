#ifndef LODEMST_H
#define LODEMST_H

#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "../node/nodeMsg.h"
#include "../common/types.h"

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

            // ~Master(){
            //     close(sockfd);
            //     unlink(sockaddr.sun_path);
            // }

            // TODO: finish constructor
            // Master(std::string sockPath){
            //     sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
            //     if(sockfd < 0)
            //         throw "Error creating socket";

            //     sockaddr.sun_family = AF_LOCAL;
            //     std::strcpy(sockaddr.sun_path, sockPath.c_str());

            //     if(bind(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr_un))){
            //         throw "Error binding socket";
            //     };
            // }

        private:
            int sockfd;           ///< master listening socket file descriptor.
            sockaddr_un sockaddr;
            char buffer[1024];    ///< buffer that the listening socket uses.

            topicTreeNode* rootNode = new topicTreeNode; ///< tree of directories and topics.
            std::vector<node> nodeArray;                 ///< array of nodes connected to this master.

            /**
             * Tokenizes (separates) path string into indexable vector.
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
    };
}

#endif
