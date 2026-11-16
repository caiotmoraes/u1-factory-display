#pragma once
struct HTTPClient {
    void setConnectTimeout(int) {}
    void setTimeout(int) {}
    bool begin(const char *) { return false; }
    int POST(const char *) { return -1; }
    void end() {}
};
