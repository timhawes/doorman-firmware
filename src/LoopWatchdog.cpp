// SPDX-FileCopyrightText: 2023 Tim Hawes
//
// SPDX-License-Identifier: MIT

#include "LoopWatchdog.hpp"

LoopWatchdog::LoopWatchdog() {
    uint32_t data;
    ESP.rtcUserMemoryRead(rtc_offset, &data, 4);
    if (data == rtc_magic) {
        restarted_flag = true;
        data = 0;
        ESP.rtcUserMemoryWrite(rtc_offset, &data, 4);
    }
}

void LoopWatchdog::callback() {
    if ((long)(millis() - last_feed) > timeout) {
        uint32_t data = rtc_magic;
        ESP.rtcUserMemoryWrite(rtc_offset, &data, 4);
        Serial.println("LoopWatchdog: restarting");
        ESP.restart();
    }
}

void LoopWatchdog::begin() {
    last_feed = millis();
    ticker.attach(1, std::bind(&LoopWatchdog::callback, this));
}

void LoopWatchdog::feed() {
    last_feed = millis();
}

bool LoopWatchdog::restarted() {
    return restarted_flag;
}
