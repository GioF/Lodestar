#include <cstdint>
#include <cstring>

// NOTE: maybe should extract string copying into separate function

namespace Lodestar {
    enum msgtype: uint8_t{authNode, topicReg, topicUpd, shutdwn};

    struct registration {
        uint8_t type;        ///< type of registration; 0 for insertion into topic, 1 for deletion
        uint8_t topicType;   ///< type of topic; 0 for pub, 1 for sub
        uint16_t nameLen;         ///< length of topic name
        char* name;          ///< topic name
        uint16_t registrarLen;    ///< length of registrar name
        char* registrarName; ///< registrar name
    };

    /**
     * Serialize registration.
     *
     * @param[out] buffer The buffer data is serialized into. 
     * @param[in] data The registration struct to be serialized.
     * 
     * @returns amount of bytes written.
     * */
    int serializeRegistration(char* buffer, registration data){
        uint16_t i, j; // two iterators for the two variable size strings
        int offset;
        
        buffer[0] = data.type;
        buffer[1] = data.topicType;
        buffer[2] = data.nameLen;
        buffer[3] = data.nameLen >> 8;

        // i is offset by 4 to compensate for above utilization of buffer
        offset = 4;
        for(i = offset; i - offset < data.nameLen; i++){
            buffer[i] = data.name[i - offset];
        }

        buffer[i] = data.registrarLen;
        buffer[i + 1] = data.registrarLen >> 8;

        offset = i + 2;
        for(j = offset; j - offset < data.registrarLen; j++){
            buffer[j] = data.registrarName[j - offset];
        }
        return j;
    }

    /**
     * Deserialize registration.
     *
     * @param[in] buffer Buffer to be unserialized.
     *
     * @returns Unserialized data.
     * */
    registration deserializeRegistration(char* buffer){
        registration data;
        data.name = new char[1024];
        data.registrarName = new char[1024];
        uint16_t i, j;
        int offset;

        data.type = buffer[0];
        data.topicType = buffer[1];
        //copy 16 bits of buffer (offset by 2) into nameLen's address (which was cast into a char)
        std::memcpy((char*)&(data.nameLen), &buffer[2], sizeof(uint16_t));

        offset = 4;
        for(i = offset; i - offset < data.nameLen; i++){
            data.name[i - offset] = buffer[i];
        }

        std::memcpy((char*)&(data.registrarLen), &buffer[i], sizeof(uint16_t));
        
        offset = i + 2;
        for(j = offset; j - offset < data.registrarLen; j++){
            data.registrarName[j - offset] = buffer[j];
        }
        
        return data;
    }

    struct topicUpdate {
        uint8_t type;          ///< type of update; 0 for addition, 1 for removal
        uint16_t registrarLen; ///< registrar name length
        char* registrarName;   ///< name of registrar, used by the node and master to differentiate registrars
        uint16_t addressLen;   ///< address length
        char* address;         ///< address of updated topic
    };

    /**
     * Serialize update.
     *
     * @param[out] buffer The buffer data is serialized into.
     * @param[in] update The topicUpdate struct to be serialized.
     * 
     * @returns amount of bytes written.
     * */
    int serializeUpdate(char* buffer, topicUpdate update){
        uint16_t i, j;
        int offset;

        buffer[0] = update.type;
        buffer[1] = update.registrarLen;
        buffer[2] = update.registrarLen >> 8;

        offset = 3;
        for(i = offset; i - offset < update.registrarLen; i++){
            buffer[i] = update.registrarName[i - offset];
        }

        buffer[i] = update.addressLen;
        buffer[i + 1] = update.addressLen >> 8;

        offset = i + 2;
        for(j = offset; j - offset < update.addressLen; j++){
            buffer[j] = update.address[j - offset];
        }

        return j;
    }

    /**
     * Deserialize update.
     * 
     * @param[in] buffer Buffer to be unserialized.
     *
     * @returns unserialized data.
     * */
    topicUpdate deserializeUpdate(char* buffer){
        topicUpdate data;
        data.registrarName = new char[1024];
        data.address = new char[1024];
        uint16_t i, j;
        int offset;

        data.type = buffer[0];
        std::memcpy((char*)&(data.registrarLen), &buffer[1], sizeof(uint16_t));

        offset = 3;
        for(i = offset; i - offset < data.registrarLen; i++){
            data.registrarName[i - offset] = buffer[i];
        }
        
        std::memcpy((char*)&(data.addressLen), &buffer[i], sizeof(uint16_t));

        offset = i + 2;
        for(j = offset; j - offset < data.addressLen; j++){
            data.address[j - offset] = buffer[j];
        }

        return data;
    }

    struct shutdown {
        uint8_t code; ///< code of shutdown, denoting reason for it.
    };

    int serializeShutdown(char* buffer, shutdown data){
        buffer[0] = data.code;
        return 1;
    }

    shutdown deserializeShutdown(char* buffer){
        shutdown data;
        data.code = buffer[0];
        return data;
    }

    struct auth {
        int8_t size;        ///< negative for size of session id, positive for size of master password
        char* identifier;    ///< either password or session id; see size
    };

    int serializeAuth(char* buffer, auth data){
        uint8_t i, size;
        int offset;

        buffer[0] = data.size;
        (data.size < 0)?
            size = -data.size:
            size = data.size;

        offset = 1;
        for(i = offset; i - offset < size; i++){
            buffer[i] = data.identifier[i - offset];
        }

        return i;
    }

    auth deserializeAuth(char* buffer){
        uint8_t i, size;
        auth data;
        data.size = buffer[0];
            data.identifier = new char[128];

        (data.size < 0)?
            size = -data.size:
            size = data.size;
        
        int offset = 1;
        for(i = offset; i - offset < size; i++){
            data.identifier[i - offset] = buffer[i];
        }

        return data;
    }

    struct commonMessage {
        msgtype type;
        union {
            registration* topicRegMsg;
            topicUpdate* topicUpdMsg;
            shutdown* shutdwnMsg;
            auth* authNodeMsg;
        } data;
    };

    /**
     * Serializes a message.
     *
     * Based on message type, the data pointer is casted into the respective struct to be used
     * as such, and then passed into the second argument of the respective function.
     *
     * @param[in] msg a pointer to the struct that represents the message.
     * @param[out] buffer the buffer data will be serialized to.
     * 
     * @returns amount of bytes written.
     * */
    int serializeMessage(commonMessage* msg, char* buffer){
        int size = 1;
        buffer[0] = msg->type;
        switch (msg->type){ 
            case msgtype::authNode:
                size += serializeAuth(&buffer[1], *(msg->data.authNodeMsg));
                 break;
            case msgtype::topicReg:
                size += serializeRegistration(&buffer[1], *(msg->data.topicRegMsg));
                 break;
            case msgtype::topicUpd:
                size += serializeUpdate(&buffer[1], *(msg->data.topicUpdMsg));
                 break;
            case msgtype::shutdwn:
                size += serializeShutdown(&buffer[1], *(msg->data.shutdwnMsg));
                break;
        }
        return size;
    }

    /**
     * Deserialize a message.
     *
     * The returned struct's message type will be set to the correct enum.
     *
     * @param[in] buffer the buffer containing the serialized message.
     * @returns a pointer to the deserialized message.
     * */
    commonMessage* deserializeMessage(char* buffer){
        commonMessage* msg;
        msg->type = static_cast<msgtype>(buffer[0]);
        switch (msg->type){ 
            case msgtype::authNode:
                msg->data.authNodeMsg = new auth;
                *(msg->data.authNodeMsg) = deserializeAuth(&buffer[1]);
                return msg;
            case msgtype::topicReg:
                msg->data.topicRegMsg = new registration;
                *(msg->data.topicRegMsg) = deserializeRegistration(&buffer[1]);
                return msg;
            case msgtype::topicUpd:
                msg->data.topicUpdMsg = new topicUpdate;
                *(msg->data.topicUpdMsg) = deserializeUpdate(&buffer[1]);
                return msg;
            case msgtype::shutdwn:
                msg->data.shutdwnMsg = new shutdown;
                *(msg->data.shutdwnMsg) = deserializeShutdown(&buffer[1]);
                return msg;
            default:
                return NULL;
        }
    }
}
