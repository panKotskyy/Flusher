#ifndef TELEGRAM_BOT_H
#define TELEGRAM_BOT_H

#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>

class TelegramBot {
public:
    TelegramBot();

    void init();
    void handleTelegram();

private:
    UniversalTelegramBot bot;
    WiFiClientSecure client;
    unsigned long lastTimeBotRan;
    int botRequestDelay = 5000; // Checks for new messages every 1 second.
    void handleNewMessages(int numNewMessages);
};

#endif // TELEGRAM_BOT_H
