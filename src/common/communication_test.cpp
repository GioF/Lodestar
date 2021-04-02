#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "communication.cpp"
#include "doctest.h"
#include <cstdlib>

TEST_CASE("Node registration protocol"){
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

TEST_CASE("Node update protocol"){
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
