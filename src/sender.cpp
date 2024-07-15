#include "MulticastSender.h"

Message::Message(MessageType type, int seq, const std::string &msg)
    : type(type), sequenceNumber(seq), nodeId(0)
{
    strncpy(content, msg.c_str(), sizeof(content));
}

ReceiverNode::ReceiverNode(int ack, int id) : ackSequenceNumber(ack), nodeId(id) {}

bool ReceiverNode::operator==(const ReceiverNode &other) const
{
    return nodeId == other.nodeId;
}

MulticastSender::MulticastSender(const std::string &multicastAddress, int port)
    : multicastAddress(multicastAddress), port(port), sequenceNumber(0), lastAckExchange(0), callback(nullptr), running(false)
{
    // 按IPv4和UDP协议创建套接字
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(multicastAddress.c_str());
    addr.sin_port = htons(port);

    // 设置套接字为非阻塞模式
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // 初始化定时器和发送断点
    lastAckTime = std::chrono::steady_clock::now();
    sendPointer = sendQueue.begin();
}

MulticastSender::~MulticastSender()
{
    stop();
    close(sockfd);
}

bool MulticastSender::sendMessage(const std::string &message)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    Message msg(DATA, sequenceNumber, message);
    if (!sendQueue.push_back(msg))
    {
        return false;
    }
    std::cout << "Enqueued: " << msg.sequenceNumber << ": " << msg.content << std::endl;
    sequenceNumber++;
    return true;
}

void MulticastSender::start()
{
    running = true;
    senderThread = std::thread(&MulticastSender::run, this);
}

void MulticastSender::stop()
{
    running = false;
    if (senderThread.joinable())
    {
        senderThread.join();
    }
}

void MulticastSender::run()
{
    fd_set readFds;
    while (running)
    {
        FD_ZERO(&readFds);
        FD_SET(sockfd, &readFds);

        int ret = select(sockfd + 1, &readFds, nullptr, nullptr, nullptr);
        if (ret > 0 && FD_ISSET(sockfd, &readFds))
        {
            Message msg(INIT, 0, 0, "");
            socklen_t len = sizeof(addr);
            int n = recvfrom(sockfd, &msg, sizeof(msg), 0, (struct sockaddr *)&addr, &len);
            if (n > 0)
            {
                switch (msg.type)
                {
                case ACK:
                    handleACK(msg);
                    break;
                case NACK:
                    handleNACK(msg);
                    break;
                default:
                    auto now = std::chrono::steady_clock::now();
                    std::chrono::duration<double> elapsedSeconds = now - lastAckTime;
                    if (sequenceNumber - lastAckExchange + 1 >= SEND_ACK_COUNT || elapsedSeconds.count() >= ackTimeout)
                    {
                        requestACK();
                        lastAckTime = now;
                    }

                    sendPendingMessages();
                    break;
                }
            }
        }
        else
        {
            // 无事件，处理发包逻辑
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsedSeconds = now - lastAckTime;
            if (sequenceNumber - lastAckExchange + 1 >= SEND_ACK_COUNT || elapsedSeconds.count() >= ackTimeout)
            {
                requestACK();
                lastAckTime = now;
            }

            sendPendingMessages();
        }
    }
}

// 设置回调函数
void MulticastSender::setCallback(std::function<void(const Event &)> cb)
{
    callback = cb;
}

void MulticastSender::requestACK()
{
    std::lock_guard<std::mutex> lock(queueMutex);

    // 当table为空时怎么办，当table只有一部分节点时怎么办？
    if (receiverTable.empty())
    {
        // 发送新ACK请求
        Message msg(ACK_REQUEST, 0, "");
        sendto(sockfd, &msg, sizeof(msg), 0, (const struct sockaddr *)&addr, sizeof(addr));
        std::cout << "Sent ACK Request" << std::endl;
        return;
    }

    // 清理发送缓冲区
    ReceiverNode minNode(std::numeric_limits<int>::max(), 0);
    for (const auto &node : receiverTable)
    {
        if (node.ackSequenceNumber < minNode.ackSequenceNumber)
            minNode = node;
    }

    // 记录当前ACK
    lastAckExchange = minNode.ackSequenceNumber;

    // 遍历 sendQueue，移除所有 sequenceNumber 小于 minNode 的消息
    auto it = sendQueue.begin();
    if (it != sendQueue.end() && it->sequenceNumber <= minNode.ackSequenceNumber)
    {
        while (it != sendQueue.end())
        {
            if (it->sequenceNumber <= minNode.ackSequenceNumber)
                it = sendQueue.erase(it);
            else
                ++it;
        }
    }
    else
    {
        // 踢除ACK发送过慢的节点，当都很慢时怎么办？
        if (sendPointer->sequenceNumber > minNode.ackSequenceNumber + DELETE_COUNT)
            receiverTable.erase(minNode);
    }

    // 发送新ACK请求
    Message msg(ACK_REQUEST, 0, "");
    sendto(sockfd, &msg, sizeof(msg), 0, (const struct sockaddr *)&addr, sizeof(addr));
    std::cout << "Sent ACK Request" << std::endl;
}

void MulticastSender::handleACK(const Message &msg)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    std::cout << "Received ACK: " << msg.sequenceNumber << std::endl;

    // 按id检查接收方是否在表中
    auto it = receiverTable.find(ReceiverNode(0, msg.nodeId));

    if (it != receiverTable.end())
    {
        // 接收方已存在，更新较大的ack
        if (it->ackSequenceNumber < msg.sequenceNumber)
        {
            ReceiverNode updatedNode = *it;
            updatedNode.ackSequenceNumber = msg.sequenceNumber;
            receiverTable.erase(it);
            receiverTable.insert(updatedNode);
        }
    }
    else
    {
        // 接收方不存在，添加新节点
        ReceiverNode newReceiver(msg.sequenceNumber, msg.nodeId);
        receiverTable.insert(newReceiver);
    }
}

void MulticastSender::handleNACK(const Message &msg)
{
    std::string nackContent(msg.content);
    size_t pos = nackContent.find(" ");
    int startSeq = std::stoi(nackContent.substr(0, pos));
    int endSeq = std::stoi(nackContent.substr(pos + 1));
    std::cout << "Received NACK for range: " << startSeq << " - " << endSeq << std::endl;

    if (startSeq < sendQueue.front().sequenceNumber || endSeq > sequenceNumber)
    {
        // 回调无法处理的事件
        return;
    }

    std::lock_guard<std::mutex> lock(queueMutex);

    // 二分查找NACK的起始节点
    auto it = std::lower_bound(sendQueue.begin(), sendQueue.end(), startSeq,
                               [](const Message &msg, int seq)
                               { return msg.sequenceNumber < seq; });

    // 从找到的起始点开始顺序遍历补包
    for (; it != sendQueue.end() && it->sequenceNumber <= endSeq; ++it)
    {
        // 这里it.sequenceNumber已经保证是[startSeq, endSeq]之间的值
        it->type = REPAIR;
        sendto(sockfd, &(*it), sizeof(*it), 0, (const struct sockaddr *)&addr, sizeof(addr));
        std::cout << "Retransmitted: " << it->sequenceNumber << ": " << it->content << std::endl;
    }
}

void MulticastSender::sendPendingMessages()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    int sendCount = 0;

    // 每次发送50个包
    while (sendPointer != sendQueue.end() && sendCount < SEND_COUNT)
    {
        sendto(sockfd, &*sendPointer, sizeof(*sendPointer), 0, (const struct sockaddr *)&addr, sizeof(addr));
        std::cout << "Sent: " << sendPointer->sequenceNumber << ": " << sendPointer->content << std::endl;
        sendPointer++;
        sendCount++;
    }
}

int main()
{
    MulticastSender sender("239.255.0.1", 30001);

    sender.start();

    for (int i = 0; i < 100; ++i)
    {
        sender.sendMessage("Message " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::seconds(10));
    sender.stop();

    return 0;
}