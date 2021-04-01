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

    std::string path = "major/minor/topic";
    Lodestar::Master::topicTreeNode major;
    Lodestar::Master::topicTreeNode minor;
    Lodestar::Master::topicTreeNode topic;


    SUBCASE("Path tokenization/separation"){
        std::vector<std::string> supposedPath;
        supposedPath.push_back("major");
        supposedPath.push_back("minor");
        supposedPath.push_back("topic");

        std::vector<std::string> returnedPath = master.tokenizeTopicStr(path);

        REQUIRE(returnedPath == supposedPath);
    }

    SUBCASE("Topic finding"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("major/minor/topic");
        
        major.name = "major";
        major.type = Lodestar::Master::nodeType::dir;
        master.rootNode->subNodes.push_back(major);

        minor.name = "minor";
        minor.type = Lodestar::Master::nodeType::dir;
        major.subNodes.push_back(minor);

        topic.name = "topic";
        topic.type = Lodestar::Master::nodeType::topic;
        minor.subNodes.push_back(topic);

        Lodestar::Master::topicTreeNode* returnedTopic;
        returnedTopic = master.getDir(dirPath);

        REQUIRE(returnedTopic == &topic);
    }
}
