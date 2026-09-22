#pragma once
inline uint32_t testRandom = 12345, testRestartAt = 0;
inline unsigned testRestartCount = 0;
struct TestRestart {};
inline uint32_t esp_random(){return testRandom;}
inline void esp_restart(){++testRestartCount;testRestartAt=testClock;throw TestRestart{};}
