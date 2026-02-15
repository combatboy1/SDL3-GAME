#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "network_common.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

struct RegisteredServer {
    ServerInfo info;
    std::string ip;
    uint16_t port;
    std::chrono::steady_clock::time_point lastHeartbeat;
};

std::map<std::string, RegisteredServer> servers;

std::string getPublicIPv6Address() {
    // Get adapter addresses
    ULONG bufferSize = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);

    if (pAddresses == NULL) {
        return "Unable to allocate memory";
    }

    ULONG result = GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
        NULL, pAddresses, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);
        if (pAddresses == NULL) {
            return "Unable to allocate memory";
        }
        result = GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            NULL, pAddresses, &bufferSize);
    }

    if (result != NO_ERROR) {
        free(pAddresses);
        return "Error getting adapters";
    }

    std::vector<std::string> addresses;

    // Iterate through adapters
    PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
    while (pCurrAddresses) {
        // Skip loopback and non-operational adapters
        if (pCurrAddresses->OperStatus == IfOperStatusUp) {
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;

            while (pUnicast) {
                if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6) {
                    sockaddr_in6* sa6 = (sockaddr_in6*)pUnicast->Address.lpSockaddr;
                    char ipStr[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &(sa6->sin6_addr), ipStr, INET6_ADDRSTRLEN);

                    std::string addr(ipStr);

                    // Filter out link-local (fe80::) and loopback (::1) addresses
                    if (addr.substr(0, 4) != "fe80" && addr != "::1") {
                        addresses.push_back(addr);
                    }
                }
                pUnicast = pUnicast->Next;
            }
        }
        pCurrAddresses = pCurrAddresses->Next;
    }

    free(pAddresses);

    if (addresses.empty()) {
        return "No public IPv6 address found";
    }

    // Prefer addresses that start with 2 or 3 (global unicast)
    for (const auto& addr : addresses) {
        if (addr[0] == '2' || addr[0] == '3') {
            return addr;
        }
    }

    // Return first address if no global unicast found
    return addresses[0];
}

void processMessage(const std::string& message, const std::string& clientIP, uint16_t clientPort,
    SOCKET socket, sockaddr_in6& clientAddr) {

    if (message.substr(0, 9) == "REGISTER:") {
        // Format: REGISTER:name,port,currentPlayers,maxPlayers,mapW,mapH,hasPass,code
        std::string data = message.substr(9);
        std::stringstream ss(data);
        std::string token;
        std::vector<std::string> parts;

        while (std::getline(ss, token, ',')) {
            parts.push_back(token);
        }

        if (parts.size() >= 7) {
            RegisteredServer regServer;
            regServer.info.name = parts[0];
            regServer.info.address = clientIP;
            regServer.info.port = std::stoi(parts[1]);
            regServer.info.currentPlayers = std::stoi(parts[2]);
            regServer.info.maxPlayers = std::stoi(parts[3]);
            regServer.info.mapWidth = std::stoi(parts[4]);
            regServer.info.mapHeight = std::stoi(parts[5]);
            regServer.info.hasPassword = (parts[6] == "1");
            regServer.info.serverCode = (parts.size() > 7) ? parts[7] : "";
            regServer.ip = clientIP;
            regServer.port = clientPort;
            regServer.lastHeartbeat = std::chrono::steady_clock::now();

            std::string serverKey = clientIP + ":" + std::to_string(regServer.info.port);
            servers[serverKey] = regServer;

            std::cout << "[REGISTER] " << regServer.info.name << " (" << serverKey << ")" << std::endl;

            std::string response = "OK";
            sendto(socket, response.c_str(), (int)response.length(), 0,
                (sockaddr*)&clientAddr, sizeof(clientAddr));
        }
    }
    else if (message == "QUERY") {
        // Send list of all servers
        std::stringstream response;
        response << "SERVERS:";

        bool first = true;
        for (const auto& pair : servers) {
            if (!first) response << ";";
            const ServerInfo& info = pair.second.info;
            response << info.name << ","
                << info.address << ","
                << info.port << ","
                << info.currentPlayers << ","
                << info.maxPlayers << ","
                << info.mapWidth << ","
                << info.mapHeight << ","
                << (info.hasPassword ? "1" : "0") << ","
                << info.serverCode;
            first = false;
        }

        std::string responseStr = response.str();
        sendto(socket, responseStr.c_str(), (int)responseStr.length(), 0,
            (sockaddr*)&clientAddr, sizeof(clientAddr));

        std::cout << "[QUERY] Sent " << servers.size() << " servers to client" << std::endl;
    }
    else if (message.substr(0, 11) == "HEARTBEAT:") {
        std::string serverKey = message.substr(11);
        if (servers.find(serverKey) != servers.end()) {
            servers[serverKey].lastHeartbeat = std::chrono::steady_clock::now();
        }
    }
}

void checkServerTimeouts() {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> toRemove;

    for (const auto& pair : servers) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - pair.second.lastHeartbeat).count();

        if (elapsed > 60) {  // 60 second timeout
            toRemove.push_back(pair.first);
            std::cout << "[TIMEOUT] " << pair.second.info.name << " removed" << std::endl;
        }
    }

    for (const auto& key : toRemove) {
        servers.erase(key);
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in6 serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin6_family = AF_INET6;
    serverAddr.sin6_addr = in6addr_any;
    serverAddr.sin6_port = htons(SERVER_FINDER_PORT);

    DWORD ipv6only = 0;
    setsockopt(serverSocket, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only));

    u_long mode = 1;
    ioctlsocket(serverSocket, FIONBIO, &mode);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "==================================================" << std::endl;
    std::cout << "Server Finder Running on Port " << SERVER_FINDER_PORT << std::endl;
    std::cout << "==================================================" << std::endl;

    // Get and display public IPv6 address
    std::string publicIPv6 = getPublicIPv6Address();
    std::cout << "Public IPv6 Address: " << publicIPv6 << std::endl;
    std::cout << "==================================================" << std::endl;

    char buffer[4096];
    sockaddr_in6 clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    auto lastTimeoutCheck = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // Check for timeouts every 10 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTimeoutCheck).count() >= 10) {
            checkServerTimeouts();
            lastTimeoutCheck = now;
        }

        memset(buffer, 0, sizeof(buffer));
        int recvLen = recvfrom(serverSocket, buffer, sizeof(buffer) - 1, 0,
            (sockaddr*)&clientAddr, &clientAddrLen);

        if (recvLen == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                Sleep(100);
                continue;
            }
            continue;
        }

        buffer[recvLen] = '\0';
        std::string message(buffer);

        char clientIP[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &(clientAddr.sin6_addr), clientIP, INET6_ADDRSTRLEN);
        uint16_t clientPort = ntohs(clientAddr.sin6_port);

        processMessage(message, std::string(clientIP), clientPort, serverSocket, clientAddr);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}