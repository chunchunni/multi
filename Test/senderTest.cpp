#include "MulticastSender.h"
#include <iostream>
#include <thread>
#include <chrono>

void senderCallback(const Event &event)
{
    std::cout << "Sender Event: " << event.message << std::endl;
}

int main()
{
    const std::string multicastAddress = "239.0.0.1";
    const int port = 12345;

    MulticastSender sender(multicastAddress, port);
    sender.setCallback(senderCallback);

    sender.start();

    // 模拟发送消息
    std::this_thread::sleep_for(std::chrono::seconds(1));
    sender.sendMessage("Hello, this is a test message!");
    std::cout << "Sent: Hello, this is a test message!" << std::endl;

    // 等待一些时间以发送更多消息
    for (int i = 0; i < 10; ++i)
    {
        std::string message = "Test message " + std::to_string(i);
        sender.sendMessage(message);
        std::cout << "Sent: " << message << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 停止发送
    sender.stop();

    return 0;
}
