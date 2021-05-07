#ifndef LODEMTYPES_H
#define LODEMTYPES_H
#include <string>
#include <vector>
#include <list>
#include <mutex>
#include "../common/types.h"
#include "../common/communication.cpp"

namespace Lodestar{
    /**
     * A struct that represents nodes which register to topics.
     * */
    struct registrar {
        std::string address;  ///< string used by the node to identify an instance of a publisher/subscriber.*/
        int nodeSocketFd;     ///< the socket file descriptor of the node.*/
    };
    
    /**
     * A struct that defined a node of the topic tree.
     *
     * Node, in this context, is used to refer to an element in a tree,
     * instead of a component in a distributed system.
     * */
    struct topicTreeNode {
        nodeType type;                       ///< The type of the tree node; a directory of topics or a topic.
        std::string name;                    ///< The name of the topic or directory.
        std::vector<topicTreeNode> subNodes; ///< Subdirectories of a directory; empty if a topic.
        std::vector<registrar> publishers;   ///< a vector of nodes that publish to this topic; empty if a directory.
        std::vector<registrar> subscribers;  ///< a vector of nodes that subscribe to this topic; empty if a directory.
    };
    
    /**
     * A struct that represents a reference to a topic, from the node array.
     * */
    struct topicTreeRef {
        std::string address;         ///< the same as the registrar address.
        topicTreeNode* topicPointer; ///< a pointer to the subscriber topic.
        registrar* directPointer;    ///< a direct pointer to the registrar.
    };
    
    /**
     * A struct that represents a connected node.
     *
     * Notice that this is the system common meaning of a "node";
     * a component in a distributed system, not a leaf in a tree.
     * */
    struct connectedNode {
        int socketFd;                          ///< The file descriptor of the nodes' socket.
        std::vector<topicTreeRef> publishers;  ///< vector of topics the node publishes to.
        std::vector<topicTreeRef> subscribers; ///< vector of topics the node subscribes to.
    };
    
    /**
     * A struct that represents an authenticable socket.
     *
     * An authenticable socket is a socket which is connected, but is yet to
     * send an identifier or a session ID to be treated as a node which is authorized
     * to communicate with the Master.
     * */
    struct autheableNode {
        std::mutex lock;
        message authmsg; ///< where the auth message will be
        std::chrono::time_point<std::chrono::steady_clock> timeout; ///< when it will timeout
        int sockfd = 0;      ///< the file descriptor of the authenticable socket
        bool active = true;
        
        autheableNode(const autheableNode& node){
            authmsg = node.authmsg;
            timeout = node.timeout;
            sockfd = node.sockfd;
            active = node.active;
        }
        
        autheableNode(){}
    };
}
#endif
