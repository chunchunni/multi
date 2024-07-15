#ifndef MULTICASTSENDER_H
#define MULTICASTSENDER_H

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <mutex>
#include <algorithm>

enum MessageType
{
    INIT,
    DATA,
    ACK,
    NACK,
    ACK_REQUEST,
    REPAIR
};

struct Message
{
    MessageType type;
    int sequenceNumber;
    char content[256];
    int nodeId;

    Message(MessageType type, int seq, int id, const std::string &msg)
        : type(type), sequenceNumber(seq), nodeId(0)
    {
        strncpy(content, msg.c_str(), sizeof(content));
    }
};

struct ReceiverNode
{
    int ackSequenceNumber;
    int nodeId;

    ReceiverNode(int ack, int id);

    bool operator==(const ReceiverNode &other) const;
};

// 定义回调事件类型枚举
enum EventType
{
    INQUEUE_ERROR,
    NACK_OUT_QUEUE
};

struct Event
{
    EventType type;      // 事件类型
    std::string message; // 事件消息内容
};

class MulticastSender
{
public:
    MulticastSender(const std::string &multicastAddress, int port);
    ~MulticastSender();

    bool sendMessage(const std::string &message);

    void setCallback(std::function<void(const Event &)> cb);

    void start();
    void stop();

private:
    void run();

    void requestACK();
    void handleACK(const Message &msg);
    void handleNACK(const Message &msg);
    void sendPendingMessages();

    int sockfd;
    struct sockaddr_in addr;
    std::string multicastAddress;
    int port;
    int sequenceNumber;
    int lastAckExchange;
    std::deque<Message>::iterator sendPointer;
    std::deque<Message> sendQueue;
    std::unordered_set<ReceiverNode> receiverTable;
    std::mutex queueMutex;
    std::chrono::time_point<std::chrono::steady_clock> lastAckTime;
    std::function<void(const Event &)> callback;

    std::atomic<bool> running;
    std::thread senderThread;
};

#endif // MULTICASTSENDER_H
