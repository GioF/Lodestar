#ifndef LODECOMM_H
#define LODECOMM_H

#include <cstdint>
#include <cstring>
#include <chrono>
#include <stdexcept>
#include <tuple>
#include <sys/socket.h>
#include "types.h"
#include <errno.h>

// NOTE: maybe should extract string copying into separate function

namespace Lodestar {

    //communication-specific exceptions
    class timeoutException: public std::runtime_error{
        public:
            using std::runtime_error::runtime_error;

            std::chrono::milliseconds timeout(){
                return time;
            }

            timeoutException(std::chrono::milliseconds time, std::string message):time(time), std::runtime_error(message){}

        private:
            std::chrono::milliseconds time;
    };

    //basic message types
    struct registration: public transmittable{
        uint8_t type;        ///< type of registration; 0 for insertion into topic, 1 for deletion
        uint8_t topicType;   ///< type of topic; 0 for pub, 1 for sub
        uint16_t nameLen;         ///< length of topic name
        char* name;          ///< topic name
        uint16_t registrarLen;    ///< length of registrar name
        char* registrarName; ///< registrar name

        registration(){
            dataType = msgtype::authNode;
        }
        
        int serialize(char* buffer){
            //since this function is the first, i will annotate it a bit
            //so others can have a quicker reference when debugging
            //or making additional serializations
            uint16_t i, j; // two iterators for the two variable size strings
            int offset;
            
            buffer[0] = type;
            buffer[1] = topicType;
            buffer[2] = nameLen;
            buffer[3] = nameLen >> 8;
            
            // i is offset by 4 to compensate for above utilization of buffer
            offset = 4;
            for(i = offset; i - offset < nameLen; i++){
                buffer[i] = name[i - offset];
            }
            
            buffer[i] = registrarLen;
            buffer[i + 1] = registrarLen >> 8;
            
            offset = i + 2;
            for(j = offset; j - offset < registrarLen; j++){
                buffer[j] = registrarName[j - offset];
            }
            return j;
        }

        void deserialize(char* buffer){
            name = new char[1024];
            registrarName = new char[1024];
            uint16_t i, j;
            int offset;
            
            type = buffer[0];
            topicType = buffer[1];
            //copy 16 bits of buffer (offset by 2) into nameLen's address (which was cast into a char)
            std::memcpy((char*)&(nameLen), &buffer[2], sizeof(uint16_t));
            
            offset = 4;
            for(i = offset; i - offset < nameLen; i++){
                name[i - offset] = buffer[i];
            }
            
            std::memcpy((char*)&(registrarLen), &buffer[i], sizeof(uint16_t));
            
            offset = i + 2;
            for(j = offset; j - offset < registrarLen; j++){
                registrarName[j - offset] = buffer[j];
            }
        }
    };

    struct topicUpdate: public transmittable{
        uint8_t type;          ///< type of update; 0 for addition, 1 for removal
        uint16_t registrarLen; ///< registrar name length
        char* registrarName;   ///< name of registrar, used by the node and master to differentiate registrars
        uint16_t addressLen;   ///< address length
        char* address;         ///< address of updated topic

            topicUpdate(){
                dataType = msgtype::topicUpd;
            }
            
        int serialize(char* buffer){
            uint16_t i, j;
            int offset;
            
            buffer[0] = type;
            buffer[1] = registrarLen;
            buffer[2] = registrarLen >> 8;
            
            offset = 3;
            for(i = offset; i - offset < registrarLen; i++){
                buffer[i] = registrarName[i - offset];
            }
            
            buffer[i] = addressLen;
            buffer[i + 1] = addressLen >> 8;
            
            offset = i + 2;
            for(j = offset; j - offset < addressLen; j++){
                buffer[j] = address[j - offset];
            }
            
            return j;
        }

        void deserialize(char* buffer){
            registrarName = new char[1024];
            address = new char[1024];
            uint16_t i, j;
            int offset;
            
            type = buffer[0];
            std::memcpy((char*)&(registrarLen), &buffer[1], sizeof(uint16_t));
            
            offset = 3;
            for(i = offset; i - offset < registrarLen; i++){
                registrarName[i - offset] = buffer[i];
            }
            
            std::memcpy((char*)&(addressLen), &buffer[i], sizeof(uint16_t));
            
            offset = i + 2;
            for(j = offset; j - offset < addressLen; j++){
                address[j - offset] = buffer[j];
            }
        }
    };

    struct shutdown: public transmittable{
        uint8_t code; ///< code of shutdown, denoting reason for it.

        shutdown(){
            dataType = msgtype::shutdwn;
        }
        
        int serialize(char* buffer){
            buffer[0] = code;
            return 1;
        }
        
        void deserialize(char* buffer){
            code = buffer[0];
        }
    };

    struct auth: public transmittable{
        int8_t size;        ///< negative for size of session id, positive for size of master password
        char* identifier;    ///< either password or session id; see size

            auth(){
                dataType = msgtype::authNode;
            }
            
        int serialize(char* buffer){
            uint8_t i;
            int offset;
            
            buffer[0] = size;
            if(size < 0){
                size = -size;
            }
            
            offset = 1;
            for(i = offset; i - offset < size; i++){
                buffer[i] = identifier[i - offset];
            }
            
            return i;
        }
        
        void deserialize(char* buffer){
            uint8_t i;
            size = buffer[0];
            identifier = new char[128];
            
            if(size < 0){
                size = -size;
            }
            
            int offset = 1;
            for(i = offset; i - offset < size; i++){
                identifier[i - offset] = buffer[i];
            }
        }
    };

    class message{
        public:
            transmittable* data = NULL;      ///< pointer to an object that implements transmittable
            msgStatus state = msgStatus::ok; ///< current state; see msgStatus
            
            /**
             * Serializes a message.
             *
             * data.type goes into the first byte and the next byte is given as an argument
             * to the serialization function of data.
             *
             * @param[out] buffer the buffer data will be serialized to.
             * 
             * @returns amount of bytes written.
             * */
            uint16_t serializeMessage(char* buffer){
                uint16_t size = 1;
                buffer[0] = data->dataType;
                size += data->serialize(&buffer[1]);
                return size;
            }
            
            /**
             * Deserialize a message and put it into data.
             *
             * The function treats the data pointer as null, so if not vacant
             * it's best to delete the object data points to.
             *
             * @param[in] buffer the buffer containing the serialized message.
             * */
            void deserializeMessage(char* lbuffer){
                msgtype type = static_cast<msgtype>(lbuffer[0]);
                switch (type){ 
                    case msgtype::authNode:
                        data = new auth;
                        data->deserialize(&lbuffer[1]);
                        break;
                    case msgtype::topicReg:
                        data = new registration;
                        data->deserialize(&lbuffer[1]);
                        break;
                    case msgtype::topicUpd:
                        data = new topicUpdate;
                        data->deserialize(&lbuffer[1]);
                        break;
                    case msgtype::shutdwn:
                        data = new shutdown;
                        data->deserialize(&lbuffer[1]);
                        break;
                    default:
                        throw "Unknown message type";
                }
                data->dataType = type;
            }
            
            /**
             * Deserialize this message's buffer into a message.
             *
             * Simply calls deserializeMessage(char* buffer) with this object's
             * buffer as argument.
             * */
            void deserializeMessage(){
                deserializeMessage(buffer + 2);
            }
            
            /**
             * Serializes the data on the data pointer and sends it all at once.
             *
             * Will assure all bytes of message are sent, so it's best
             * to use this function asynchronously.
             *
             * @param sockfd the socket the message is to be sent.
             * @returns amounts of sent bytes, -1 on error
             * */
            int sendMessage(int sockfd){
                size = serializeMessage(&buffer[2]);
                buffer[0] = size;
                buffer[1] = size >> 8;
                
                int sent = 0;
                
                while(sent < size + 2 || sent == -1){
                    sent = sent + send(sockfd, &buffer[sent], (size + 2) - sent, 0);
                }
                
                return sent;
            }
            
            /**
             * Receives a message into a buffer to be serialized later.
             *
             * Will assure message is completely received and will block until so,
             * so this function is better used asynchronously.
             *
             * @param sockfd the socket in which the message will be received from.
             * @returns the received message.
             * */
            void recvMessage(int sockfd){
                recvMessage_for(sockfd, std::chrono::minutes(1));
            }
            
            /**
             * Receives a message for [time] microseconds.
             *
             * To be used only with sockets which timeout, with a sane amount set such that
             * the receive loop will not spin for too much and become a cpu hog or wait for
             * so long that [time] microseconds has already elapsed and the function
             * executed for too much time.
             * Will not automatically deserialize data once it finishes receiving data.
             * If the function runs for [time], a status of receiving is returned.
             * If the function finalized receiving, a status of ok is returned.
             *
             * @param sockfd the socket in which the message will be received from.
             * @param time the time which the function is to be executed for.
             * @returns the status of the message.
             * */
            msgStatus recvMessage_for(int sockfd, std::chrono::milliseconds time){
                auto began = std::chrono::steady_clock::now();
                auto timeout = std::chrono::steady_clock::now() + time;
                auto now = std::chrono::steady_clock::now();
                
                //try to read first 2 bytes and interprete them as
                //size to be read (if not already reading a message)
                if(state == msgStatus::ok){
                    auto firstResult = recv_for(2, sockfd, buffer, time);
                    
                    if(std::get<0>(firstResult) == 2){
                        std::memcpy((char*)&(size), buffer, sizeof(uint16_t));
                    }else{
                        throw timeoutException(time, "Could not receive first two bytes");
                    }
                }
                
                //actually receive the data
                auto secondResult = recv_for(size, sockfd, &buffer[received + 2], time);
                
                //store total received and remaining size
                received += std::get<0>(secondResult);
                size -= std::get<0>(secondResult);
                
                if(std::get<1>(secondResult)){
                    state = msgStatus::receiving;
                    return state;
                }
                
                //only executes if all bytes were received
                state = msgStatus::ok;
                size = 0;
                received = 0;
                return state;
            }

        private:
            char buffer[1024];
            uint16_t size = 0;
            int received = 0;

            // TODO: check if socket has non zero timeout sockopt on input and error out if not

            /**
             * Receives [size] bytes with [time] milliseconds as timeout.
             *
             * @param size amount to be received.
             * @param sockfd socket the data will be received from.
             * @param[out] buffer which data is to be received into.
             * @param time time in milliseconds which the function has to execute.
             * @returns  tuple made of, respectivelly, amount of bytes sent, and if function timed out.
             * */
            std::tuple<int, bool> recv_for(int size, int sockfd, char* buffer, std::chrono::milliseconds time){
                auto began = std::chrono::steady_clock::now(); // when function began
                auto timeout = std::chrono::steady_clock::now() + time; // time limit
                auto now = std::chrono::steady_clock::now(); // current time

                int totalReceived = 0;
                int received = 0;

                while(size > 0 && now < timeout){
                    received = recv(sockfd, &buffer[totalReceived], size, 0);
                    now = std::chrono::steady_clock::now();

                    //loop again if timed out
                    if(received == -1){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            received = 0;
                            continue;
                        }else{
                            throw "error while receiving";
                        }
                    }
                    totalReceived += received;
                    size = size - received;
                }

                bool timedOut = false;
                if(now > timeout){
                    timedOut = true;
                }

                return std::make_tuple(totalReceived, timedOut);
            }
    };

}

#endif
