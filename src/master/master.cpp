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

    //TODO: finish master node handle in order to know what to write here
    struct registrar {
        std::string handleName;
    };

    //TODO: finish master node handle in order to know what to write here
    struct topicNode {
        nodeType type;
        std::string name;
        std::vector<topicNode> subNodes;

    };

    class Master {

        public:
            ~Master(){
                close(sockfd);
                unlink(sockaddr.sun_path);
            }

            //TODO: launch listening loop
            Master(std::string sockPath){
                sockfd = socket(AF_LOCAL, SOCK_DGRAM, 0);
                if(sockfd < 0)
                    throw "Error creating socket";

                sockaddr.sun_family = AF_LOCAL;
                std::strcpy(sockaddr.sun_path, sockPath.c_str());

                if(bind(sockfd, (struct sockaddr *) &sockaddr, sizeof(sockaddr_un))){
                    throw "Error binding socket";
                };
            }

        private:
            int sockfd;
            sockaddr_un sockaddr;
            char buffer[1024];

            topicNode* rootNode;

            //TODO: test this function
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
            topicNode* getDir(std::vector<std::string> dirPath){
                topicNode *currentDir = rootNode;
                topicNode *foundDir = NULL;
                std::vector<topicNode>::iterator subNodeIterator;

                for(int i = 0; i < dirPath.size(); i++){
                    //to avoid unnecessary processing in the case
                    //the next nodes are all empty (fresh)
                    if(currentDir->subNodes.empty()){
                        currentDir->subNodes.push_back(topicNode {nodeType::dir, dirPath[i]});
                        currentDir = &currentDir->subNodes.back();
                    }

                    for(subNodeIterator = currentDir->subNodes.begin(); subNodeIterator < currentDir->subNodes.end(); subNodeIterator++){
                        if(subNodeIterator->name == dirPath[0])
                            foundDir = &(*subNodeIterator);
                    }

                    //if subnode with given name was not found, insert it
                    if(foundDir == NULL){
                        currentDir->subNodes.push_back(topicNode {nodeType::dir, dirPath[i]});
                        currentDir = &currentDir->subNodes.back();
                    } else {
                        currentDir = foundDir;
                    }
                }

                return currentDir;
            }

            //TODO: finish master node handle in order to know what to write here
            void registerInTopic(std::string path, nodeType registrantType){
                std::vector<std::string> tokenizedPath = tokenizeTopicStr(path);
                std::string topicName = tokenizedPath.back();

                tokenizedPath.pop_back();
                topicNode* dir = getDir(tokenizedPath);

                dir->subNodes.push_back(topicNode {registrantType, topicName});
            }
    };
}

#endif
