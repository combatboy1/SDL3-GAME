#pragma once
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

// Server Finder Address (configurable)
const std::string SERVER_FINDER_IP = "::1";  // Change this to your server finder IP
const int SERVER_FINDER_PORT = 7777;         // Different port from game servers

struct ServerInfo {
    std::string name;
    std::string address;
    int port;
    int currentPlayers;
    int maxPlayers;
    int mapWidth;
    int mapHeight;
    bool hasPassword;
    std::string serverCode;  // Optional join code
};

inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}