#include <cstdint>
#include <cstring>

// TODO: extract string copying into separate function

namespace Lodestar {
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
     * */
    void serializeRegistration(char* buffer, registration data){
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

        buffer[i + 1] = data.registrarLen;
        buffer[i + 2] = data.registrarLen >> 8;

        offset = i + 3;
        for(j = offset; j - offset < data.registrarLen; j++){
            buffer[j] = data.registrarName[j - offset];
        }
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

        std::memcpy((char*)&(data.registrarLen), &buffer[i + 1], sizeof(uint16_t));
        
        offset = i + 3;
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
     * */
    void serializeUpdate(char* buffer, topicUpdate update){
        uint16_t i, j;
        int offset;

        buffer[0] = update.type;
        buffer[1] = update.registrarLen;
        buffer[2] = update.registrarLen >> 8;

        offset = 3;
        for(i = offset; i - offset < update.registrarLen; i++){
            buffer[i] = update.registrarName[i - offset];
        }

        buffer[i + 1] = update.addressLen;
        buffer[i + 2] = update.addressLen >> 8;

        offset = i + 3;
        for(j = offset; j - offset < update.addressLen; j++){
            buffer[j] = update.address[j - offset];
        }
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
        
        std::memcpy((char*)&(data.addressLen), &buffer[i + 1], sizeof(uint16_t));

        offset = i + 3;
        for(j = offset; j - offset < data.addressLen; j++){
            data.address[j - offset] = buffer[j];
        }

        return data;
    }

    struct shutdown {
        uint8_t code; ///< code of shutdown, denoting reason for it.
    };

    char serializeShutdown(uint8_t code){
        return (char)code;
    }

    uint8_t deserializeShutdown(char buf){
        return (uint8_t)buf;
    }
}
