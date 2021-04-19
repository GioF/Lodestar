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

void receiveMsgfn(int sockfd, Lodestar::message* msg){
    sockaddr_un inSockaddr;
    socklen_t addrlen = sizeof(struct sockaddr_un);

    listen(sockfd, 10);
    int connectedfd = accept(sockfd, (struct sockaddr *)&inSockaddr, &addrlen);

    msg->recvMessage(connectedfd);
    msg->deserializeMessage();
}

void transmitMsgfn(int sockfd, Lodestar::message* msg, sockaddr_un addr){
    connect(sockfd, (struct sockaddr*) &addr, sizeof(sockaddr_un));
    msg->sendMessage(sockfd);
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
    dummyStruct.serialize(buffer);

    Lodestar::registration deserialized;
    deserialized.deserialize(buffer);

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
    dummyStruct.serialize(buffer);

    Lodestar::topicUpdate deserialized;
    deserialized.deserialize(buffer);

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
    dummyStruct.serialize(buffer);
    
    Lodestar::auth deserialized;
    deserialized.deserialize(buffer);

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
    dummyStruct.dataType = Lodestar::msgtype::authNode;
    Lodestar::message msg;
    msg.data = &dummyStruct;

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
    Lodestar::message receivedMessage;
    std::thread listeningThread = std::thread(&receiveMsgfn, rxfd, &receivedMessage);
    pthread_setname_np(listeningThread.native_handle(), "listener");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    //set up thread that runs the send part
    std::thread sendingThread = std::thread(&transmitMsgfn, txfd, &msg, txSockaddr);
    pthread_setname_np(sendingThread.native_handle(), "sender");

    //wait for threads and unlink socket
    listeningThread.join();
    if(sendingThread.joinable()){ 
        sendingThread.join();
    }

    unlink(rxSockaddr.sun_path);

    Lodestar::auth* received = dynamic_cast<Lodestar::auth*>(receivedMessage.data);
    Lodestar::auth* sent = dynamic_cast<Lodestar::auth*>(msg.data);

    REQUIRE(received->size == sent->size);
    //get strings from the char arrays and then compare them
    std::string dummyString = std::string(sent->identifier);
    std::string receivedString = std::string(received->identifier);
    REQUIRE(dummyString == receivedString);
}
