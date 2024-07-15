#ifndef MULTICASTRECEIVER_H
#define MULTICASTRECEIVER_H

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <deque>
#include <algorithm>
#include <chrono>
#include <functional>
#include <BPlusTree.h>
#include <atomic>

enum MessageType
{
    INIT,
    ACK,
    NACK,
    ACK_REQUEST,
    DATA,
    REPAIR
};

struct Message
{
    MessageType type;
    int sequenceNumber;
    char content[256];
    int nodeId;

    Message(MessageType type, int seq, int id, const std::string &msg)
        : type(type), sequenceNumber(seq), nodeId(id)
    {
        strncpy(content, msg.c_str(), sizeof(content));
    }

    bool operator<(const Message &other) const
    {
        return sequenceNumber < other.sequenceNumber;
    }

    bool operator==(const Message &other) const
    {
        return sequenceNumber == other.sequenceNumber;
    }
};

// 定义回调事件类型枚举
enum EventType
{
    EVENT_DATA,
    NACK_ERROR,
};

struct Event
{
    EventType type;      // 事件类型
    std::string message; // 事件消息内容
};

const double nackTimeout = 1.0; // 超时时间（秒）

class MulticastReceiver
{
public:
    MulticastReceiver(const std::string &multicastAddress, int port, int receiverId);
    ~MulticastReceiver();

    void start();
    void stop();

    void setCallback(std::function<void(const Event &)> cb);
    Message getData();

private:
    void run();
    void handleMessage(const Message &msg);
    // void processBuffer();
    void handleRepair(const Message &msg);
    void sendACK();
    void sendNACK(int startSeq, int endSeq);

    int sockfd;
    struct sockaddr_in addr;
    std::string multicastAddress;
    int port;
    int receiverId;
    int lastReceived;
    int lastAckExchange;
    std::deque<Message> receiveQueue;
    std::mutex queueMutex;
    std::function<void(const Event &)> callback;
    std::pair<int, int> nackRanges;
    int inNackRecoveryCount;
    int isSendNACK;
    BPlusTree<Message> skipCountTree;
    std::chrono::time_point<std::chrono::steady_clock> receiveSkipMsg;
    std::atomic<bool> running;
    std::thread receiverThread;
};

#endif // MULTICASTRECEIVER_H
