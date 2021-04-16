#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "communication.cpp"
#include "doctest.h"
#include <cstdlib>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <pthread.h>
#include <string>
#include <sys/un.h>

void receiveMsgfn(int sockfd, Lodestar::commonMessage* commonMsg){

    sockaddr_un inSockaddr;
    socklen_t addrlen = sizeof(struct sockaddr_un);

    listen(sockfd, 10);
    int connectedfd = accept(sockfd, (struct sockaddr *)&inSockaddr, &addrlen);

    Lodestar::commonMessage received = Lodestar::recvMessage(connectedfd);
    *commonMsg = received;
    std::cout << "got here";
}

void transmitMsgfn(int sockfd, Lodestar::commonMessage msg, sockaddr_un addr){
    connect(sockfd, (struct sockaddr*) &addr, sizeof(sockaddr_un));
    sendMessage(msg, sockfd);
}

TEST_CASE("registration - Node registration message"){
    Lodestar::registration dummyStruct;

    dummyStruct.type = 0;
    dummyStruct.topicType = 0;

    char testName[] = "testTopic";
    dummyStruct.name = &testName[0];
    dummyStruct.nameLen = 10;

    char testRegistrar[] = "testReg";
    dummyStruct.registrarName = &testRegistrar[0];
    dummyStruct.registrarLen = 8;

    char* buffer = (char*)std::malloc(1024);
    Lodestar::serializeRegistration(buffer, dummyStruct);
    Lodestar::registration deserialized = Lodestar::deserializeRegistration(buffer);

    std::string dummyNameString = dummyStruct.name;
    std::string dummyRegistrarString = dummyStruct.registrarName;
    std::string deserializedNameString = deserialized.name;
    std::string deserializedRegistrarString = deserialized.registrarName;

    CHECK(dummyStruct.type == deserialized.type);
    CHECK(dummyStruct.topicType == deserialized.topicType);
    CHECK(dummyStruct.nameLen == deserialized.nameLen);
    CHECK(dummyStruct.registrarLen == deserialized.registrarLen);
    CHECK(dummyNameString == deserializedNameString);
    CHECK(dummyRegistrarString == deserializedRegistrarString);
}

TEST_CASE("topicUpdate - Node update message"){
    Lodestar::topicUpdate dummyStruct;

    dummyStruct.type = 0;

    char testRegistrar[] = "testReg";
    dummyStruct.registrarName = &testRegistrar[0];
    dummyStruct.registrarLen = 8;

    char testAddr[] = "test/test/topic";
    dummyStruct.address = &testAddr[0];
    dummyStruct.addressLen = 16;

    char* buffer = (char*)std::malloc(1024);
    Lodestar::serializeUpdate(buffer, dummyStruct);
    Lodestar::topicUpdate deserialized = Lodestar::deserializeUpdate(buffer);

    std::string dummyRegistrarString = dummyStruct.registrarName;
    std::string dummyAddrString = dummyStruct.address;
    std::string deserializedRegistrarString = deserialized.registrarName;
    std::string deserializedAddrString = deserialized.address;

    CHECK(dummyStruct.type == deserialized.type);
    CHECK(dummyStruct.registrarLen == deserialized.registrarLen);
    CHECK(dummyRegistrarString == deserializedRegistrarString);
    CHECK(dummyStruct.addressLen == deserialized.addressLen);
    CHECK(dummyAddrString == deserializedAddrString);
}

TEST_CASE("auth - Node authentication message"){
    Lodestar::auth dummyStruct;

    char testIdentifier[] = "samplepasswd";
    dummyStruct.identifier = testIdentifier;
    dummyStruct.size = 13;

    char* buffer = (char*)std::malloc(1024);
    Lodestar::serializeAuth(buffer, dummyStruct);
    Lodestar::auth deserialized = Lodestar::deserializeAuth(buffer);

    std::string dummyIdentifier = dummyStruct.identifier;
    std::string deserializedIdentifier = deserialized.identifier;
    
    CHECK(dummyStruct.size == deserialized.size);
    CHECK(dummyIdentifier == deserializedIdentifier);
}

TEST_CASE("Common Message Transmission and reception"){
    //setting up message
    Lodestar::auth dummyStruct;
    char testIdentifier[] = "samplepasswd";
    dummyStruct.identifier = testIdentifier;
    dummyStruct.size = 13;
    Lodestar::commonMessage dummyMsg;
    dummyMsg.type = Lodestar::msgtype::authNode;
    dummyMsg.data.authNodeMsg = &dummyStruct;

    //set up sockets
    int rc;

    std::string socketPath = std::string(getenv("PWD"));
    socketPath.append("/listener.socket");

    int txfd = socket(AF_LOCAL, SOCK_STREAM, 0);
    sockaddr_un txSockaddr;
    txSockaddr.sun_family = AF_LOCAL;
    std::strcpy(txSockaddr.sun_path, "listener.socket");

    int rxfd = socket(AF_LOCAL, SOCK_STREAM, 0);
    sockaddr_un rxSockaddr;
    rxSockaddr.sun_family = AF_LOCAL;
    std::strcpy(rxSockaddr.sun_path, socketPath.c_str());

    rc = bind(rxfd, (struct sockaddr *) &rxSockaddr, sizeof(sockaddr_un));
    REQUIRE(rc == 0);

    //set up thread that runs the receive part
    Lodestar::commonMessage receivedMessage;
    std::thread listeningThread = std::thread(&receiveMsgfn, rxfd, &receivedMessage);
    pthread_setname_np(listeningThread.native_handle(), "listener");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    //set up thread that runs the send part
    std::thread sendingThread = std::thread(&transmitMsgfn, txfd, dummyMsg, txSockaddr);
    pthread_setname_np(sendingThread.native_handle(), "sender");

    //wait for threads and unlink socket
    sendingThread.join();
    if(listeningThread.joinable()){ 
        listeningThread.join();
    }

    unlink(rxSockaddr.sun_path);

    REQUIRE(receivedMessage.data.authNodeMsg->size == dummyMsg.data.authNodeMsg->size);
    //get strings from the char arrays and then compare them
    std::string dummyString = std::string(dummyMsg.data.authNodeMsg->identifier);
    std::string receivedString = std::string(receivedMessage.data.authNodeMsg->identifier);
    REQUIRE(dummyString == receivedString);
}
