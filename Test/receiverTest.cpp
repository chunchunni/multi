#include "MulticastReceiver.h"
#include <iostream>
#include <thread>
#include <chrono>

void receiverCallback(const Event &event)
{
    std::cout << "Receiver Event: " << event.message << std::endl;
}

int main()
{
    const std::string multicastAddress = "239.0.0.1";
    const int port = 12345;
    const int receiverId = 1;

    MulticastReceiver receiver(multicastAddress, port, receiverId);
    receiver.setCallback(receiverCallback);

    receiver.start();

    std::thread receiveThread([&receiver]()
                              {
        while (true)
        {
            Message msg = receiver.getData();
            std::cout << "Received: " << msg.content << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } });

    // 等待一段时间以接收消息
    std::this_thread::sleep_for(std::chrono::minutes(2));

    // 停止接收
    receiver.stop();
    receiveThread.join();

    return 0;
}

// g++ -std=c++11 -pthread -o sender sender_main.cpp MulticastSender.cpp
// g++ -std=c++11 -pthread -o receiver receiver_main.cpp MulticastReceiver.cpp

// g++ -I..\include -I..\src .\receiverTest.cpp
