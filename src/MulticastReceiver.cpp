#include "MulticastReceiver.h"

MulticastReceiver::MulticastReceiver(const std::string &multicastAddress, int port, int receiverId,
                                     std::function<void(const Message &)> callback)
    : multicastAddress(multicastAddress), port(port), receiverId(receiverId), lastReceived(-1),
      inNackRecoveryCount(0), isSendNACK(0), skipCountTree(3), callback(nullptr), running(false)
{
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicastAddress.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
    {
        perror("setsockopt failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 设置套接字为非阻塞模式
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

MulticastReceiver::~MulticastReceiver()
{
    close(sockfd);
}

// 用于设置回调函数
void MulticastReceiver::setCallback(std::function<void(const Event &)> cb)
{
    callback = cb;
}

void MulticastReceiver::start()
{
    running = true;
    receiverThread = std::thread(&MulticastReceiver::run, this);
}

void MulticastReceiver::stop()
{
    running = false;
    if (receiverThread.joinable())
    {
        receiverThread.join();
    }
}

void MulticastReceiver::run()
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
                case DATA:
                    handleMessage(msg);
                    break;
                case ACK_REQUEST:
                    sendACK();
                    break;
                case REPAIR:
                    handleRepair(msg);
                    break;
                default:
                    // processBuffer();
                    break;
                }
            }
        }
        // else
        // {
        //     processBuffer();
        // }
    }
}

void MulticastReceiver::handleMessage(const Message &msg)
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (msg.sequenceNumber <= lastReceived)
    {
        // 去掉重复的包
        return;
    }
    else if (msg.sequenceNumber == lastReceived + 1)
    {
        receiveQueue.push_back(msg);
        lastReceived++;
        // processBuffer();
    }
    else
    {
        if (inNackRecoveryCount == 0)
        {
            // 第一次乱序到达，将msg放入排序树中，并将标志位置为1，激活乱序定时
            skipCountTree.insert(msg);
            inNackRecoveryCount = 1;
            receiveSkipMsg = std::chrono::steady_clock::now();
        }
        else
        {
            // 已有乱序状态，将msg放入排序树中，若未超时，则结束处理
            skipCountTree.insert(msg);
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsedSeconds = now - receiveSkipMsg;

            // 若超时，则从树中读取数据，依次放入队列中
            if (elapsedSeconds.count() >= nackTimeout)
            {
                Message minMsg = skipCountTree.getMin();
                while (!skipCountTree.isEmpty() && minMsg.sequenceNumber >= lastReceived)
                {
                    if (minMsg.sequenceNumber == lastReceived + 1)
                    {
                        receiveQueue.push_back(minMsg);
                        lastReceived++;
                        skipCountTree.deleteMin();
                    }
                    else if (minMsg.sequenceNumber <= lastReceived)
                    {
                        // 去掉重复的包
                        skipCountTree.deleteMin();
                    }
                    else
                    {
                        // 发现空洞
                        break;
                    }
                }

                if (skipCountTree.isEmpty())
                {
                    // 树中至少有一个跳包，此时已补完
                    inNackRecoveryCount = 0;
                    isSendNACK = 0;
                }
                else if (isSendNACK != 0)
                {
                    if (lastReceived >= nackRanges.second)
                    {
                        inNackRecoveryCount = 0;
                        isSendNACK = 0;
                    }
                    else
                    {
                        // 回调应用处理
                    }
                }
                else
                {
                    // 发送NACK，记录发送状态并重置定时器
                    sendNACK(lastReceived + 1, minMsg.sequenceNumber - 1);
                    isSendNACK = 1;
                    receiveSkipMsg = std::chrono::steady_clock::now();
                }
            }
        }
    }
}

// void MulticastReceiver::processBuffer()
// {
//     std::lock_guard<std::mutex> lock(queueMutex);
//     // 按顺序处理消息
//     auto it = receiveQueue.begin();
//     while (it != receiveQueue.end())
//     {
//         callback(*it);
//         lastReceived = it->sequenceNumber;
//         it = receiveQueue.erase(it);
//     }
// }

Message MulticastReceiver::getData()
{
    std::lock_guard<std::mutex> lock(queueMutex);

    if (!receiveQueue.empty())
    {
        Message frontMessage = receiveQueue.front();
        receiveQueue.pop_front();
        return frontMessage;
    }
    return NULL;
}

void MulticastReceiver::handleRepair(const Message &msg)
{
    if (isSendNACK == 0)
        return;

    if (msg.sequenceNumber < nackRanges.first || msg.sequenceNumber > nackRanges.second)
        return;

    handleMessage(msg);
}

void MulticastReceiver::sendACK()
{
    Message msg(ACK, lastAckExchange, receiverId, "");
    sendto(sockfd, &msg, sizeof(msg), 0, (const struct sockaddr *)&addr, sizeof(addr));
    std::cout << "Sent ACK: " << msg.sequenceNumber << std::endl;
}

void MulticastReceiver::sendNACK(int startSeq, int endSeq)
{
    Message msg(NACK, 0, receiverId, std::to_string(startSeq) + " " + std::to_string(endSeq));
    sendto(sockfd, &msg, sizeof(msg), 0, (const struct sockaddr *)&addr, sizeof(addr));
    std::cout << "Sent NACK for range: " << startSeq << " - " << endSeq << std::endl;
    nackRanges.first = startSeq;
    nackRanges.second = endSeq;
}

int main()
{
    auto callback = [](const Message &msg)
    {
        std::cout << "Processing message: " << msg.sequenceNumber << ": " << msg.content << std::endl;
    };

    MulticastReceiver receiver("239.255.0.1", 30001, 1);

    receiver.start();

    std::this_thread::sleep_for(std::chrono::seconds(10));

    receiver.stop();

    return 0;
}
