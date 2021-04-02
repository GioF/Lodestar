#include <cstdint>
#include <cstring>

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
     * @param[out] buffer The buffer data is serialized onto. 
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
}
