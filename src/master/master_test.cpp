#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <chrono>
#include <sys/socket.h>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include "master.cpp"
#include "../common/communication.cpp"
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
            std::list<Lodestar::connectedNode>* nodeArray = NULL;

            //threading variables
            bool* isOk;
            int* sockfd;
            sockaddr_un* sockaddr;
            std::thread *listeningThread = NULL;
            std::list<Lodestar::autheableNode>* authQueue = NULL;
            int* nThreads;
            semaphore* authAwaitSignal;
            semaphore* authWaitingSignal;
            semaphore* authContinueSignal;

            void setupPointers(){
                rootNode = master->rootNode;
                nodeArray = &(master->nodeArray);
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

            void authorizeNodes(int timeout){
                master->authorizeNodes(authQueue, std::chrono::milliseconds(timeout));
            }
            
            void cleanupQueue(int cutoff){
                master->cleanAuthQueue(cutoff);
            }
    };
}

TEST_CASE("Master - business logic"){
    Lodestar::Master_test master;

    std::string path = "dir1/dir2/lastdir";
    Lodestar::topicTreeNode dir1;
    Lodestar::topicTreeNode dir2;
    Lodestar::topicTreeNode lastdir;

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

        Lodestar::topicTreeNode* returnedDir;
        returnedDir = master.getDir(dirPath);

        REQUIRE(returnedDir->name == lastdir.name);
        REQUIRE(returnedDir->type == lastdir.type);
    }

    SUBCASE("getDir - directory insertion"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1/dir2/lastdir");

        Lodestar::topicTreeNode* firstReturnedDir;
        Lodestar::topicTreeNode* secondReturnedDir;

        firstReturnedDir = master.getDir(dirPath);
        secondReturnedDir = master.getDir(dirPath);

        REQUIRE(firstReturnedDir == secondReturnedDir);
    }

    SUBCASE("getTopic - topic finding"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1/dir2");
        Lodestar::topicTreeNode* dir;
        dir = master.getDir(dirPath);

        Lodestar::topicTreeNode* returnedTopic;
        returnedTopic = master.getTopic(dir, "topic");

        REQUIRE(returnedTopic == NULL);

        Lodestar::topicTreeNode topic;
        topic.name = "topic";
        topic.type = Lodestar::nodeType::topic;
        dir->subNodes.push_back(topic);

        returnedTopic = master.getTopic(dir, "topic");
        REQUIRE(returnedTopic->name == topic.name);
    }

    SUBCASE("registerToTopic - topic registration/insertion"){
        std::vector<std::string> dirPath = master.tokenizeTopicStr("dir1");
        Lodestar::topicTreeNode* dir;
        dir = master.getDir(dirPath);

        int nodeSocket = 0;
        std::string registrarType = "pub";
        std::string address = "sample";
        Lodestar::registrar* registrar = NULL;

        SUBCASE("registerToTopic - topic doesn't exist yet"){
            master.registerToTopic("dir1/topic", registrarType, nodeSocket, address);
            registrar = &(dir->subNodes[0].publishers[0]);
            REQUIRE(registrar->address == address);
            REQUIRE(registrar->nodeSocketFd == nodeSocket);
        }

        SUBCASE("registerToTopic - topic exists"){
            Lodestar::topicTreeNode* returnedTopic;
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

    SUBCASE("authorizeNodes"){
        Lodestar::autheableNode dummyEntry;
        dummyEntry.timeout = std::chrono::steady_clock::now();
        dummyEntry.sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
        dummyEntry.active = true;

        sockaddr_un mSockaddr;
        mSockaddr.sun_family = AF_LOCAL;
        std::strcpy(mSockaddr.sun_path, "listener.socket");

        SUBCASE("general socket error"){
            close(dummyEntry.sockfd);

            master.authQueue->push_back(dummyEntry);
            master.authorizeNodes(100);

            REQUIRE(!master.authQueue->front().active);
        }

        SUBCASE("default operation"){
            //connect a dummy socket and check if it's on the queue
            int dummySock = socket(AF_LOCAL, SOCK_STREAM, 0);
            connect(dummySock, (struct sockaddr*) &mSockaddr, sizeof(sockaddr_un));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            //make socket timeout after blocking for 100 milliseconds
            timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            setsockopt(master.authQueue->front().sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeval));
            
            REQUIRE(master.authQueue->front().active);

            SUBCASE("message timed out"){
                //set up queue entry to timeout immediately
                master.authQueue->front().timeout = std::chrono::steady_clock::now();
                
                //try to authorize without sending nothing so it times out
                master.authorizeNodes(100);
                REQUIRE(!master.authQueue->front().active);
            }

            SUBCASE("no message sent"){
                //set up very long timeout so it doesn't timeout
                auto entryTimeout = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                master.authQueue->front().timeout = entryTimeout;

                master.authorizeNodes(100);
                REQUIRE(master.authQueue->front().active);
            }

            SUBCASE("unfinished message"){
                //set up very long timeout so it doesn't timeout
                auto entryTimeout = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                master.authQueue->front().timeout = entryTimeout;
                
                //send size header and nothing more
                int dummybuf = 10;
                auto sent = send(dummySock, &dummybuf, 2, 0);
                
                master.authorizeNodes(100);
                REQUIRE(master.authQueue->front().active);
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
                master.authorizeNodes(100);
                REQUIRE(!master.authQueue->front().active);
                REQUIRE(master.nodeArray->size() == 1);
            }
        }
    }
    
    SUBCASE("cleanupQueue - entry deletion and thread sincronization"){
        Lodestar::autheableNode dummyEntry;
        dummyEntry.active = false;
        dummyEntry.timeout = std::chrono::steady_clock::now();
        *master.nThreads = 1;

        //insert two entries and see if they are there
        master.authQueue->push_back(dummyEntry);
        master.authQueue->push_back(dummyEntry);
        REQUIRE(master.authQueue->size() == 2);

        //signal deletion function that the auth thread is
        //waiting and then call cleanup
        master.authWaitingSignal->post();
        master.cleanupQueue(1);

        //check if only one await signal was sent
        REQUIRE(master.authAwaitSignal->try_wait());
        REQUIRE(!master.authAwaitSignal->try_wait());

        //check if only one continue signal was sent
        REQUIRE(master.authContinueSignal->try_wait());
        REQUIRE(!master.authContinueSignal->try_wait());

        //check that both entries were deleted
        REQUIRE(master.authQueue->size() == 0);

        //insert another entry
        master.authQueue->push_back(dummyEntry);
        REQUIRE(master.authQueue->size() == 1);

        //check if it was deleted again
        master.authWaitingSignal->post();
        master.cleanupQueue(1);
        REQUIRE(master.authQueue->size() == 0);
    }
}
