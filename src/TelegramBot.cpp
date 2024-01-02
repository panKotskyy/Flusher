#include "TelegramBot.h"
#include <Arduino.h>

#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"

TelegramBot::TelegramBot() : bot(BOTtoken, client) {}

void TelegramBot::init() {
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot.sendMessage(CHAT_ID, "Flusher Started", "");
}

void TelegramBot::handleTelegram() {
    if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() == WL_CONNECTED)) {
        if (millis() > lastTimeBotRan + botRequestDelay) {
            int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

            while (numNewMessages) {
                Serial.println("got response");
                handleNewMessages(numNewMessages);
                numNewMessages = bot.getUpdates(bot.last_message_received + 1);
            }
            lastTimeBotRan = millis();
        }
    }
}

void TelegramBot::handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }

    // Print the received message
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    Serial.printf("Received message: %s\nFrom: %s\n", text, from_name);

    // Handle commands...
    if (text == "help") {
      bot.sendMessageWithReplyKeyboard(CHAT_ID, "Choose command:", "", "[[\"/state\", \"/flush\"]]", true);
    }
    if (text == "/flush") {
      bot.sendMessage(chat_id, "flush", "");
    }
    if (text == "/state") {
      bot.sendMessage(chat_id, "state", "");
    }
  }
}
