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
    Lodestar::Master::topicTreeNode dir1;
    Lodestar::Master::topicTreeNode dir2;
    Lodestar::Master::topicTreeNode lastdir;

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
        
        dir1.name = "dir1";
        dir1.type = Lodestar::Master::nodeType::dir;

        dir2.name = "dir2";
        dir2.type = Lodestar::Master::nodeType::dir;

        lastdir.name = "lastdir";
        lastdir.type = Lodestar::Master::nodeType::topic;

        dir1.subNodes.push_back(lastdir);
        dir2.subNodes.push_back(dir2);
        master.rootNode->subNodes.push_back(dir1);

        Lodestar::Master::topicTreeNode* returnedDir;
        returnedDir = master.getDir(dirPath);

        REQUIRE(returnedDir->name == lastdir.name);
        REQUIRE(returnedDir->type == lastdir.type);
    }

    SUBCASE("getDir - directory insertion"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1/dir2/lastdir");

        Lodestar::Master::topicTreeNode* firstReturnedDir;
        Lodestar::Master::topicTreeNode* secondReturnedDir;

        firstReturnedDir = master.getDir(dirPath);
        secondReturnedDir = master.getDir(dirPath);

        REQUIRE(firstReturnedDir == secondReturnedDir);
    }

    SUBCASE("getTopic - topic finding"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1/dir2");
        Lodestar::Master::topicTreeNode* dir;
        dir = master.getDir(dirPath);

        Lodestar::Master::topicTreeNode* returnedTopic;
        returnedTopic = master.getTopic(dir, "topic");

        REQUIRE(returnedTopic == NULL);

        Lodestar::Master::topicTreeNode topic;
        topic.name = "topic";
        topic.type = Lodestar::Master::nodeType::topic;
        dir->subNodes.push_back(topic);

        returnedTopic = master.getTopic(dir, "topic");
        REQUIRE(returnedTopic->name == topic.name);
    }
}
