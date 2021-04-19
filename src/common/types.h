#ifndef LODETYPES_H
#define LODETYPES_H
#include <cstdint>

namespace Lodestar{
    enum nodeType{dir, topic};

    enum msgtype: uint8_t{authNode, topicReg, topicUpd, shutdwn};

    enum msgStatus {ok, receiving};

    class transmittable{
        public:
            msgtype dataType;
            
            /**
             * Serialize the object.
             *
             * @param[out] buffer The buffer data is to be serialized into. 
             * 
             * @returns amount of bytes written.
             * */
            int virtual serialize(char* buffer) = 0;
            
            /**
             * Deserialize the object.
             *
             * @param[in] buffer Buffer that contains the object data.
             *
             * */
            void virtual deserialize(char* buffer) = 0;
    };
}

#endif
