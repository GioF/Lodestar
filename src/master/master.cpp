#ifndef LODEMST_H
#define LODEMST_H

#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "../node/nodeMsg.h"

namespace Lodestar::Master {

    enum nodeType {topic, dir};

    struct registrar {
        std::string address;
        int nodeSocketFd;   //maybe should be a pointer to the node array
    };

    struct topicTreeNode {
        nodeType type;
        std::string name;
        std::vector<topicTreeNode> subNodes;
        std::vector<registrar> publishers;
        std::vector<registrar> subscribers;
    };

    struct topicTreeRef {
        std::string address;
        topicTreeNode* topicPointer;
        registrar* directPointer;
    };

    struct node {
        int socketFd;
        std::vector<topicTreeRef> publishers;
        std::vector<topicTreeRef> subscribers;
    };

    class Master {
        friend class Master_test;

        public:
            // ~Master(){
            //     close(sockfd);
            //     unlink(sockaddr.sun_path);
            // }

            //TODO: finish constructor
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
            int sockfd;
            sockaddr_un sockaddr;
            char buffer[1024];

            topicTreeNode* rootNode = new topicTreeNode;
            std::vector<node> nodeArray;

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

            //TODO: test this function
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

            //TODO: test this function
            topicTreeNode* getTopic(topicTreeNode* dir, std::string topicName){
                topicTreeNode* topic = NULL;

                std::vector<topicTreeNode>::iterator it;
                for(it = dir->subNodes.begin(); it != dir->subNodes.end(); it++){
                    if(it->type == nodeType::topic && it->name == topicName)
                        topic = &(*it);
                }

                return topic;
            }

            //TODO: also insert topic into node on nodeArray
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
