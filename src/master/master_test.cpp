#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "master.cpp"
#include "../common/doctest.h"
#include "../common/types.h"

namespace Lodestar{
    class Master_test: Master{
        public:
            Master_test(std::string sockPath): Master(){
                master = new Master(sockPath);
                rootNode = master->rootNode;
            };

            Master_test(){
                master = new Master();
                rootNode = master->rootNode;
            }

            ~Master_test(){
                delete master;
                delete rootNode;
            };
            
            Master *master = NULL;
            topicTreeNode* rootNode = NULL;

            std::vector<std::string> tokenizeTopicStr(std::string path){
                return master->tokenizeTopicStr(path);
            };

            topicTreeNode* getDir(std::vector<std::string> dirPath){
                return master->getDir(dirPath);
            };

            topicTreeNode* getTopic(topicTreeNode* dir, std::string topicName){
                return master->getTopic(dir, topicName);
            };

            void registerToTopic(std::string path, std::string registrarType, int nodeSocket, std::string address){
                return master->registerToTopic(path, registrarType, nodeSocket, address);
            };
            
    };
}

TEST_CASE("Master - business logic"){
    std::string socketPath = std::string(getenv("HOME"));
    socketPath.append("/.local/share/lodestar/mastersocket");

    // NOTE: should call constructor which does not set up
    // a listener socket so it can be tested separately
    Lodestar::Master_test master(socketPath);

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
        dir1.type = Lodestar::nodeType::dir;

        dir2.name = "dir2";
        dir2.type = Lodestar::nodeType::dir;

        lastdir.name = "lastdir";
        lastdir.type = Lodestar::nodeType::topic;

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
        topic.type = Lodestar::nodeType::topic;
        dir->subNodes.push_back(topic);

        returnedTopic = master.getTopic(dir, "topic");
        REQUIRE(returnedTopic->name == topic.name);
    }

    SUBCASE("registerToTopic - topic registration/insertion"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1");
        Lodestar::Master::topicTreeNode* dir;
        dir = master.getDir(dirPath);

        int nodeSocket = 0;
        std::string registrarType = "pub";
        std::string address = "sample";
        Lodestar::Master::registrar* registrar = NULL;

        SUBCASE("registerToTopic - topic doesn't exist yet"){
            master.registerToTopic("dir1/topic", registrarType, nodeSocket, address);
            registrar = &(dir->subNodes[0].publishers[0]);
            REQUIRE(registrar->address == address);
            REQUIRE(registrar->nodeSocketFd == nodeSocket);
        }

        SUBCASE("registerToTopic - topic exists"){
            Lodestar::Master::topicTreeNode* returnedTopic;
            returnedTopic = master.getTopic(dir, "topic");

            master.registerToTopic("dir1/topic", registrarType, nodeSocket, address);
            registrar = &(dir->subNodes[0].publishers[0]);
            REQUIRE(registrar->address == address);
            REQUIRE(registrar->nodeSocketFd == nodeSocket);
        }
    }
}
