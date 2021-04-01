#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "master.cpp"
#include "../common/doctest.h"

namespace Lodestar::Master {
    class Master_test: Master{
        public:
            Master master;
            topicTreeNode* rootNode = master.rootNode;

            std::vector<std::string> tokenizeTopicStr(std::string path){
                return master.tokenizeTopicStr(path);
            };

            topicTreeNode* getDir(std::vector<std::string> dirPath){
                return master.getDir(dirPath);
            };

            topicTreeNode* getTopic(topicTreeNode* dir, std::string topicName){
                return master.getTopic(dir, topicName);
            };

            void registerToTopic(std::string path, std::string registrarType, int nodeSocket, std::string address){
                return master.registerToTopic(path, registrarType, nodeSocket, address);
            };
            
    };
}

TEST_CASE("Master"){
    Lodestar::Master::Master_test master;

    std::string path = "dir1/dir2/lastdir";
    Lodestar::Master::topicTreeNode major;
    Lodestar::Master::topicTreeNode minor;
    Lodestar::Master::topicTreeNode topic;


    SUBCASE("tokenizeTopicStr - separate path into vector"){
        std::vector<std::string> supposedPath;
        supposedPath.push_back("dir1");
        supposedPath.push_back("dir2");
        supposedPath.push_back("lastdir");

        std::vector<std::string> returnedPath = master.tokenizeTopicStr(path);

        REQUIRE(returnedPath == supposedPath);
    }

    SUBCASE("getDir - directory finding"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1/dir2/lastdir");
        
        major.name = "dir1";
        major.type = Lodestar::Master::nodeType::dir;

        minor.name = "dir2";
        minor.type = Lodestar::Master::nodeType::dir;

        topic.name = "lastdir";
        topic.type = Lodestar::Master::nodeType::topic;

        minor.subNodes.push_back(topic);
        major.subNodes.push_back(minor);
        master.rootNode->subNodes.push_back(major);

        Lodestar::Master::topicTreeNode* returnedTopic;
        returnedTopic = master.getDir(dirPath);

        REQUIRE(returnedTopic->name == topic.name);
        REQUIRE(returnedTopic->type == topic.type);
    }
}
