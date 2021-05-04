#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <sys/socket.h>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include "master.cpp"
#include "../common/doctest.h"
#include "../common/types.h"

using semaphore = boost::interprocess::interprocess_semaphore;

namespace Lodestar{
    class Master_test: Master{
        public:
            Master_test(std::string sockPath): Master(){
                master = new Master(sockPath);
                setupPointers();
            };

            Master_test(){
                master = new Master();
                rootNode = master->rootNode;
                setupPointers();
            }

            ~Master_test(){
                delete master;
                delete rootNode;
            };
            
            Master *master = NULL;
            topicTreeNode* rootNode = NULL;

            //threading variables
            bool* isOk;
            int* sockfd;
            sockaddr_un* sockaddr;
            std::thread *listeningThread = NULL;
            std::list<Lodestar::Master::autheableNode>* authQueue = NULL;
            int* nThreads;
            semaphore* authAwaitSignal;
            semaphore* authWaitingSignal;
            semaphore* authContinueSignal;

            void setupPointers(){
                rootNode = master->rootNode;
                authQueue = &(master->authQueue);
                isOk = &(master->isOk);
                sockfd = &(master->sockfd);
                sockaddr = &(master->sockaddr);
                nThreads = &(master->nThreads);
                authAwaitSignal = &(master->authAwaitSignal);
                authWaitingSignal = &(master->authWaitingSignal);
                authContinueSignal = &(master->authContinueSignal);
            };
            
            //mirroed(?) master class private methods
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

            void attachListener(){
                listeningThread = master->listeningThread;
            }

            void cleanupQueue(int cutoff){
                master->cleanupQueue(authQueue, cutoff);
            }
    };
}

TEST_CASE("Master - business logic"){
    Lodestar::Master_test master;

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

// NOTE: should test non-local networking since host info can be gotten from both sides
TEST_CASE("Master - local networking logic"){
    std::string socketPath = std::string(getenv("PWD"));
    socketPath.append("/listener.socket");

    Lodestar::Master_test master(socketPath);

    // NOTE: if this fails, try to raise the time this thread sleeps
    // in order to give more time for the listener thread to receive the
    // connection before the test sets isOk to false
    SUBCASE("listenForNodes - add to queue"){
        sockaddr_un testSockaddr;
        testSockaddr.sun_family = AF_LOCAL;
        std::strcpy(testSockaddr.sun_path, "listener.socket");

        int testSockfd = socket(AF_LOCAL, SOCK_STREAM, 0);

        CHECK(testSockfd > 0);

        // HACK: this is horrible, need to find a better method to wait for thread to be ok
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int rc = connect(testSockfd, (struct sockaddr*) &testSockaddr, sizeof(sockaddr_un));

        *(master.isOk) = false;
        master.attachListener();
        master.listeningThread->join();

        REQUIRE(rc == 0);
        REQUIRE(master.authQueue->size() == 1);
        REQUIRE(master.authQueue->front().sockfd > 0);
    }

    SUBCASE("cleanupQueue - entry deletion and thread sincronization"){
        Lodestar::Master::autheableNode dummyEntry;
        dummyEntry.active = false;
        dummyEntry.timeout = std::chrono::steady_clock::now();
        *master.nThreads = 1;

        master.authQueue->push_back(dummyEntry);
        master.authQueue->push_back(dummyEntry);
        REQUIRE(master.authQueue->size() == 2);

        master.authWaitingSignal->post();
        master.cleanupQueue(1);
        REQUIRE(master.authAwaitSignal->try_wait());
        REQUIRE(!master.authAwaitSignal->try_wait());
        REQUIRE(master.authContinueSignal->try_wait());
        REQUIRE(!master.authContinueSignal->try_wait());
        REQUIRE(master.authQueue->size() == 0);

        master.authQueue->push_back(dummyEntry);
        REQUIRE(master.authQueue->size() == 1);

        master.authWaitingSignal->post();
        master.cleanupQueue(1);
        REQUIRE(master.authQueue->size() == 0);
    }
}
