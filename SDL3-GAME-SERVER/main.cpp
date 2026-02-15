#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network_common.h"

#pragma comment(lib, "ws2_32.lib")

// Server Finder configuration (must match server_finder)
const std::string SERVER_FINDER_IP_ADDR = "::1";  // Change to your server finder IP
const int SERVER_FINDER_PORT_NUM = 7777;

// Configuration variables (loaded from file)
int MAP_WIDTH = 10000;
int MAP_HEIGHT = 10000;
int MAX_PLAYERS = 50;
std::string SERVER_NAME = "A Blob Game Server!";
std::string SERVER_CODE = "";  // Optional join code
float FOOD_PERCENTAGE = 0.01f;
int FOOD_SPAWN_PER_TICK = 2;
int PING_TIMEOUT_SECONDS = 30;
int INACTIVITY_TIMEOUT_SECONDS = 600;
float MOVE_SPEED_BASE = 10.0f;
float GROWTH_RATE_FOOD = 0.02f;
float GROWTH_RATE_PLAYER = 0.04f;
float PLAYER_START_SIZE_PERCENTAGE = 0.002f;
float PLAYER_MAX_SIZE_PERCENTAGE = 0.02f;
int GAME_SERVER_PORT = 8888;

int MAX_FOOD = 500;
const int MAX_FOOD_IN_PACKET = 200;
const int PING_INTERVAL_SECONDS = 10;

float PLAYER_START_SIZE = 20.0f;
float MIN_PLAYER_SIZE = 10.0f;
float MAX_PLAYER_SIZE = 200.0f;
float FOOD_SIZE = 5.0f;

struct FoodDot {
    int id;
    float x;
    float y;
    uint8_t r, g, b;
};

struct Cell {
    float x;
    float y;
    float size;
};

struct PlayerData {
    std::string uuid;
    std::string name;
    std::vector<Cell> cells;
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
    std::string lastSeenIP;
    uint16_t lastSeenPort;
    std::chrono::steady_clock::time_point lastPingResponse;
    std::chrono::steady_clock::time_point lastMovement;
    std::chrono::steady_clock::time_point lastPingSent;
    std::chrono::steady_clock::time_point lastSplit;
    std::chrono::steady_clock::time_point lastMerge;
};

std::random_device rd;
std::mt19937 gen(rd());

void calculateGameSizes() {
    int smallestDimension = (MAP_WIDTH < MAP_HEIGHT) ? MAP_WIDTH : MAP_HEIGHT;
    PLAYER_START_SIZE = smallestDimension * PLAYER_START_SIZE_PERCENTAGE;
    MAX_PLAYER_SIZE = smallestDimension * PLAYER_MAX_SIZE_PERCENTAGE;
    MIN_PLAYER_SIZE = PLAYER_START_SIZE * 0.5f;
    FOOD_SIZE = PLAYER_START_SIZE * 0.25f;
    float mapArea = MAP_WIDTH * MAP_HEIGHT;
    float foodArea = 3.14159f * FOOD_SIZE * FOOD_SIZE;
    MAX_FOOD = (int)((mapArea * FOOD_PERCENTAGE) / foodArea);
    if (MAX_FOOD < 10) MAX_FOOD = 10;
    if (MAX_FOOD > 10000) MAX_FOOD = 10000;
}

bool loadConfig() {
    std::ifstream configFile("server_config.txt");

    if (!configFile.is_open()) {
        std::ofstream newConfig("server_config.txt");
        newConfig << "# Blob Game Server Configuration\n";
        newConfig << "# Edit values below and restart the server\n\n";
        newConfig << "SERVER_NAME=A Blob Game Server\n";
        newConfig << "SERVER_CODE=\n";
        newConfig << "GAME_SERVER_PORT=8888\n";
        newConfig << "MAP_WIDTH=10000\n";
        newConfig << "MAP_HEIGHT=10000\n";
        newConfig << "MAX_PLAYERS=50\n\n";
        newConfig << "# Food percentage: How much of the map can be covered with food (0.01 = 1%, 0.5 = 50%)\n";
        newConfig << "FOOD_PERCENTAGE=0.05\n";
        newConfig << "FOOD_SPAWN_PER_TICK=2\n\n";
        newConfig << "# Player size scaling: Percentage of smallest map dimension\n";
        newConfig << "PLAYER_START_SIZE_PERCENTAGE=0.002\n";
        newConfig << "PLAYER_MAX_SIZE_PERCENTAGE=0.02\n\n";
        newConfig << "# Timeout settings\n";
        newConfig << "PING_TIMEOUT_SECONDS=30\n";
        newConfig << "INACTIVITY_TIMEOUT_SECONDS=600\n\n";
        newConfig << "MOVE_SPEED_BASE=10\n\n";
        newConfig << "# Growth rates: Constant growth (does not scale with player size)\n";
        newConfig << "GROWTH_RATE_FOOD=0.04\n";
        newConfig << "GROWTH_RATE_PLAYER=0.04\n";
        newConfig.close();

        std::cout << "==================================================" << std::endl;
        std::cout << "CONFIG FILE CREATED: server_config.txt" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "Please edit the configuration file and restart the server." << std::endl;
        std::cout << "\nPress any key to exit..." << std::endl;
        std::cin.get();
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(configFile, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));

        try {
            if (key == "SERVER_NAME") SERVER_NAME = value;
            else if (key == "SERVER_CODE") SERVER_CODE = value;
            else if (key == "GAME_SERVER_PORT") GAME_SERVER_PORT = std::stoi(value);
            else if (key == "MAP_WIDTH") MAP_WIDTH = std::stoi(value);
            else if (key == "MAP_HEIGHT") MAP_HEIGHT = std::stoi(value);
            else if (key == "MAX_PLAYERS") MAX_PLAYERS = std::stoi(value);
            else if (key == "FOOD_PERCENTAGE") FOOD_PERCENTAGE = std::stof(value);
            else if (key == "FOOD_SPAWN_PER_TICK") FOOD_SPAWN_PER_TICK = std::stoi(value);
            else if (key == "PLAYER_START_SIZE_PERCENTAGE") PLAYER_START_SIZE_PERCENTAGE = std::stof(value);
            else if (key == "PLAYER_MAX_SIZE_PERCENTAGE") PLAYER_MAX_SIZE_PERCENTAGE = std::stof(value);
            else if (key == "PING_TIMEOUT_SECONDS") PING_TIMEOUT_SECONDS = std::stoi(value);
            else if (key == "INACTIVITY_TIMEOUT_SECONDS") INACTIVITY_TIMEOUT_SECONDS = std::stoi(value);
            else if (key == "MOVE_SPEED_BASE") MOVE_SPEED_BASE = std::stof(value);
            else if (key == "GROWTH_RATE_FOOD") GROWTH_RATE_FOOD = std::stof(value);
            else if (key == "GROWTH_RATE_PLAYER") GROWTH_RATE_PLAYER = std::stof(value);
        }
        catch (const std::exception& e) {
            std::cout << "Error parsing line " << lineNum << std::endl;
        }
    }

    configFile.close();
    calculateGameSizes();
    return true;
}

void registerWithServerFinder(int currentPlayers) {
    static SOCKET finderSocket = INVALID_SOCKET;
    static sockaddr_in6 finderAddr;
    static bool initialized = false;

    if (!initialized) {
        finderSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (finderSocket == INVALID_SOCKET) return;

        memset(&finderAddr, 0, sizeof(finderAddr));
        finderAddr.sin6_family = AF_INET6;
        finderAddr.sin6_port = htons(SERVER_FINDER_PORT_NUM);
        inet_pton(AF_INET6, SERVER_FINDER_IP_ADDR.c_str(), &finderAddr.sin6_addr);
        initialized = true;
    }

    std::stringstream ss;
    ss << "REGISTER:" << SERVER_NAME << ","
        << GAME_SERVER_PORT << ","
        << currentPlayers << ","
        << MAX_PLAYERS << ","
        << MAP_WIDTH << ","
        << MAP_HEIGHT << ","
        << (SERVER_CODE.empty() ? "0" : "1") << ","
        << SERVER_CODE;

    std::string message = ss.str();
    sendto(finderSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&finderAddr, sizeof(finderAddr));
}

std::string generateUUID() {
    std::uniform_int_distribution<uint64_t> dis;
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);
    ss << std::setw(8) << (part1 >> 32)
        << "-" << std::setw(4) << ((part1 >> 16) & 0xFFFF)
        << "-" << std::setw(4) << (part1 & 0xFFFF)
        << "-" << std::setw(4) << (part2 >> 48)
        << "-" << std::setw(12) << (part2 & 0xFFFFFFFFFFFF);
    return ss.str();
}

void generatePlayerColor(uint8_t& r, uint8_t& g, uint8_t& b) {
    std::uniform_int_distribution<int> colorChoice(0, 11);
    int choice = colorChoice(gen);
    switch (choice) {
    case 0:  r = 255; g = 100; b = 100; break;
    case 1:  r = 100; g = 255; b = 100; break;
    case 2:  r = 100; g = 100; b = 255; break;
    case 3:  r = 255; g = 255; b = 100; break;
    case 4:  r = 255; g = 100; b = 255; break;
    case 5:  r = 100; g = 255; b = 255; break;
    case 6:  r = 255; g = 150; b = 100; break;
    case 7:  r = 150; g = 100; b = 255; break;
    case 8:  r = 255; g = 100; b = 150; break;
    case 9:  r = 150; g = 255; b = 100; break;
    case 10: r = 100; g = 150; b = 255; break;
    case 11: r = 255; g = 200; b = 100; break;
    }
}

void generateFoodColor(uint8_t& r, uint8_t& g, uint8_t& b) {
    std::uniform_int_distribution<int> color(100, 255);
    r = color(gen);
    g = color(gen);
    b = color(gen);
}

float randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

void respawnPlayer(PlayerData& player) {
    player.cells.clear();
    Cell startCell;
    startCell.x = randomFloat(10, MAP_WIDTH - 10);
    startCell.y = randomFloat(10, MAP_HEIGHT - 10);
    startCell.size = PLAYER_START_SIZE;
    player.cells.push_back(startCell);
    generatePlayerColor(player.colorR, player.colorG, player.colorB);
}

void convertPlayerToFood(const PlayerData& player, std::vector<FoodDot>& food, int& nextFoodId) {
    for (const auto& cell : player.cells) {
        float cellArea = 3.14159f * cell.size * cell.size;
        float foodArea = 3.14159f * FOOD_SIZE * FOOD_SIZE;
        int foodCount = (int)(cellArea / foodArea);

        for (int i = 0; i < foodCount; i++) {
            FoodDot newFood;
            newFood.id = nextFoodId++;
            float angle = randomFloat(0, 6.28318f);
            float distance = randomFloat(0, cell.size);
            newFood.x = cell.x + cos(angle) * distance;
            newFood.y = cell.y + sin(angle) * distance;
            if (newFood.x < 5) newFood.x = 5;
            if (newFood.x > MAP_WIDTH - 5) newFood.x = MAP_WIDTH - 5;
            if (newFood.y < 5) newFood.y = 5;
            if (newFood.y > MAP_HEIGHT - 5) newFood.y = MAP_HEIGHT - 5;
            newFood.r = player.colorR;
            newFood.g = player.colorG;
            newFood.b = player.colorB;
            food.push_back(newFood);
        }
    }
}

std::string buildPlayerList(const std::map<std::string, PlayerData>& players) {
    std::stringstream ss;
    ss << "PLAYERS:";
    bool first = true;
    for (const auto& pair : players) {
        for (const auto& cell : pair.second.cells) {
            if (!first) ss << ";";
            ss << pair.second.uuid << ","
                << pair.second.name << ","
                << std::fixed << std::setprecision(2) << cell.x << ","
                << std::fixed << std::setprecision(2) << cell.y << ","
                << std::fixed << std::setprecision(2) << cell.size << ","
                << (int)pair.second.colorR << ","
                << (int)pair.second.colorG << ","
                << (int)pair.second.colorB;
            first = false;
        }
    }
    return ss.str();
}

std::string buildNearbyFoodList(const std::vector<FoodDot>& food, float playerX, float playerY, float viewDistance) {
    std::stringstream ss;
    ss << "FOOD:";
    bool first = true;
    int count = 0;

    for (const auto& f : food) {
        float dx = f.x - playerX;
        float dy = f.y - playerY;
        float distance = sqrt(dx * dx + dy * dy);

        if (distance <= viewDistance && count < MAX_FOOD_IN_PACKET) {
            if (!first) ss << ";";
            ss << f.id << ","
                << std::fixed << std::setprecision(2) << f.x << ","
                << std::fixed << std::setprecision(2) << f.y << ","
                << (int)f.r << ","
                << (int)f.g << ","
                << (int)f.b;
            first = false;
            count++;
        }
    }
    return ss.str();
}

void spawnFood(std::vector<FoodDot>& food, int& nextFoodId) {
    if (food.size() >= (size_t)MAX_FOOD) return;
    FoodDot newFood;
    newFood.id = nextFoodId++;
    newFood.x = randomFloat(5, MAP_WIDTH - 5);
    newFood.y = randomFloat(5, MAP_HEIGHT - 5);
    generateFoodColor(newFood.r, newFood.g, newFood.b);
    food.push_back(newFood);
}

bool checkCollision(float x1, float y1, float r1, float x2, float y2, float r2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    float distance = sqrt(dx * dx + dy * dy);
    return distance < (r1 + r2);
}

bool isCompleteOverlap(float x1, float y1, float r1, float x2, float y2, float r2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    float distance = sqrt(dx * dx + dy * dy);
    return (distance + r2) <= r1;
}

void checkTimeouts(std::map<std::string, PlayerData>& players, std::vector<FoodDot>& food, int& nextFoodId) {
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> playersToRemove;

    for (const auto& pair : players) {
        auto timeSincePingResponse = std::chrono::duration_cast<std::chrono::seconds>(
            now - pair.second.lastPingResponse).count();
        auto timeSinceMovement = std::chrono::duration_cast<std::chrono::seconds>(
            now - pair.second.lastMovement).count();

        if (timeSincePingResponse > PING_TIMEOUT_SECONDS) {
            playersToRemove.push_back(pair.first);
            std::cout << "[TIMEOUT] " << pair.second.name << " disconnected - converting to food" << std::endl;
        }
        else if (timeSinceMovement > INACTIVITY_TIMEOUT_SECONDS) {
            playersToRemove.push_back(pair.first);
            std::cout << "[INACTIVE] " << pair.second.name << " disconnected - converting to food" << std::endl;
        }
    }

    for (const std::string& uuid : playersToRemove) {
        convertPlayerToFood(players[uuid], food, nextFoodId);
        players.erase(uuid);
    }
}

void sendPings(std::map<std::string, PlayerData>& players, SOCKET serverSocket) {
    auto now = std::chrono::steady_clock::now();
    for (auto& pair : players) {
        PlayerData& player = pair.second;
        auto timeSinceLastPing = std::chrono::duration_cast<std::chrono::seconds>(
            now - player.lastPingSent).count();

        if (timeSinceLastPing >= PING_INTERVAL_SECONDS) {
            std::string pingMessage = "PING";
            sockaddr_in6 clientAddr;
            memset(&clientAddr, 0, sizeof(clientAddr));
            clientAddr.sin6_family = AF_INET6;
            clientAddr.sin6_port = htons(player.lastSeenPort);
            inet_pton(AF_INET6, player.lastSeenIP.c_str(), &clientAddr.sin6_addr);
            sendto(serverSocket, pingMessage.c_str(), pingMessage.length(), 0,
                (sockaddr*)&clientAddr, sizeof(clientAddr));
            player.lastPingSent = now;
        }
    }
}

void splitPlayer(PlayerData& player) {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastSplit = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - player.lastSplit).count();
    if (timeSinceLastSplit < 100) return;

    // Check if ANY cell can split (must be at least 2x MIN_PLAYER_SIZE)
    bool canSplit = false;
    for (const auto& cell : player.cells) {
        if (cell.size >= MIN_PLAYER_SIZE * 2) {
            canSplit = true;
            break;
        }
    }

    // If no cells can split, don't allow the split at all
    if (!canSplit) return;

    std::vector<Cell> newCells;
    for (const auto& cell : player.cells) {
        // Only split cells that are large enough
        if (cell.size >= MIN_PLAYER_SIZE * 2) {
            Cell cell1, cell2;
            cell1.size = cell.size / 1.414f;
            cell2.size = cell1.size;
            float offset = cell.size * 0.6f;
            cell1.x = cell.x - offset;
            cell1.y = cell.y;
            cell2.x = cell.x + offset;
            cell2.y = cell.y;
            newCells.push_back(cell1);
            newCells.push_back(cell2);
        }
        else {
            // Keep cells that are too small as-is
            newCells.push_back(cell);
        }
    }
    player.cells = newCells;
    player.lastSplit = now;
}

void mergePlayer(PlayerData& player) {
    auto now = std::chrono::steady_clock::now();
    auto timeSinceLastMerge = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - player.lastMerge).count();
    if (timeSinceLastMerge < 100) return;
    if (player.cells.size() <= 1) return;

    if (player.cells.size() >= 2) {
        float minDist = 999999.0f;
        size_t idx1 = 0, idx2 = 1;
        for (size_t i = 0; i < player.cells.size(); i++) {
            for (size_t j = i + 1; j < player.cells.size(); j++) {
                float dx = player.cells[i].x - player.cells[j].x;
                float dy = player.cells[i].y - player.cells[j].y;
                float dist = sqrt(dx * dx + dy * dy);
                if (dist < minDist) {
                    minDist = dist;
                    idx1 = i;
                    idx2 = j;
                }
            }
        }

        Cell merged;
        merged.size = sqrt(player.cells[idx1].size * player.cells[idx1].size +
            player.cells[idx2].size * player.cells[idx2].size);
        merged.x = (player.cells[idx1].x + player.cells[idx2].x) / 2;
        merged.y = (player.cells[idx1].y + player.cells[idx2].y) / 2;

        std::vector<Cell> newCells;
        for (size_t i = 0; i < player.cells.size(); i++) {
            if (i != idx1 && i != idx2) {
                newCells.push_back(player.cells[i]);
            }
        }
        newCells.push_back(merged);
        player.cells = newCells;
    }
    player.lastMerge = now;
}

int main() {
    if (!loadConfig()) {
        return 0;
    }

    WSADATA wsaData;
    SOCKET serverSocket;
    sockaddr_in6 serverAddr, clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    char buffer[4096];

    std::map<std::string, PlayerData> players;
    std::vector<FoodDot> food;
    int nextFoodId = 0;
    auto lastTimeoutCheck = std::chrono::steady_clock::now();
    auto lastFoodSpawn = std::chrono::steady_clock::now();
    auto lastPingSend = std::chrono::steady_clock::now();
    auto lastServerFinderUpdate = std::chrono::steady_clock::now();

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    serverSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin6_family = AF_INET6;
    serverAddr.sin6_addr = in6addr_any;
    serverAddr.sin6_port = htons(GAME_SERVER_PORT);

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
    std::cout << SERVER_NAME << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "Port: " << GAME_SERVER_PORT << std::endl;
    std::cout << "Map: " << MAP_WIDTH << "x" << MAP_HEIGHT << std::endl;
    std::cout << "Max Players: " << MAX_PLAYERS << std::endl;
    if (!SERVER_CODE.empty()) {
        std::cout << "Server Code: " << SERVER_CODE << std::endl;
    }
    std::cout << "==================================================" << std::endl;

    std::cout << "Spawning initial food..." << std::endl;
    for (int i = 0; i < MAX_FOOD / 2; i++) {
        spawnFood(food, nextFoodId);
    }
    std::cout << "Food spawned: " << food.size() << std::endl;

    // Register with server finder immediately
    registerWithServerFinder(players.size());

    while (true) {
        auto now = std::chrono::steady_clock::now();

        // Update server finder every 30 seconds
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastServerFinderUpdate).count() >= 30) {
            registerWithServerFinder(players.size());
            lastServerFinderUpdate = now;
        }

        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPingSend).count() >= 5) {
            sendPings(players, serverSocket);
            lastPingSend = now;
        }

        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTimeoutCheck).count() >= 5) {
            checkTimeouts(players, food, nextFoodId);
            lastTimeoutCheck = now;
        }

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFoodSpawn).count() >= 100) {
            for (int i = 0; i < FOOD_SPAWN_PER_TICK; i++) {
                spawnFood(food, nextFoodId);
            }
            lastFoodSpawn = now;
        }

        memset(buffer, 0, sizeof(buffer));
        int recvLen = recvfrom(serverSocket, buffer, sizeof(buffer) - 1, 0,
            (sockaddr*)&clientAddr, &clientAddrLen);

        if (recvLen == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                Sleep(10);
                continue;
            }
            continue;
        }

        buffer[recvLen] = '\0';
        std::string message(buffer);

        char clientIP[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &(clientAddr.sin6_addr), clientIP, INET6_ADDRSTRLEN);
        uint16_t clientPort = ntohs(clientAddr.sin6_port);

        size_t firstColon = message.find(':');
        if (firstColon == std::string::npos) continue;

        std::string receivedUUID = message.substr(0, firstColon);
        std::string remaining = message.substr(firstColon + 1);

        size_t secondColon = remaining.find(':');
        if (secondColon == std::string::npos) continue;

        std::string playerName = remaining.substr(0, secondColon);
        std::string command = remaining.substr(secondColon + 1);

        std::string playerUUID;
        std::string response;

        if (receivedUUID == "NONE" || receivedUUID.empty()) {
            // Check for server code if required
            if (!SERVER_CODE.empty()) {
                if (command.substr(0, 5) == "CODE:") {
                    std::string providedCode = command.substr(5);
                    if (providedCode != SERVER_CODE) {
                        response = "ERROR:WRONG_CODE";
                        sendto(serverSocket, response.c_str(), response.length(), 0,
                            (sockaddr*)&clientAddr, clientAddrLen);
                        continue;
                    }
                    command = "INIT";  // Continue with connection
                }
                else if (command == "INIT") {
                    response = "ERROR:CODE_REQUIRED";
                    sendto(serverSocket, response.c_str(), response.length(), 0,
                        (sockaddr*)&clientAddr, clientAddrLen);
                    continue;
                }
            }

            std::string clientKey = std::string(clientIP) + ":" + std::to_string(clientPort);
            bool alreadyConnected = false;

            for (const auto& pair : players) {
                std::string existingKey = pair.second.lastSeenIP + ":" + std::to_string(pair.second.lastSeenPort);
                if (existingKey == clientKey) {
                    alreadyConnected = true;
                    playerUUID = pair.first;
                    break;
                }
            }

            if (!alreadyConnected) {
                if (players.size() >= (size_t)MAX_PLAYERS) {
                    response = "ERROR:SERVER_FULL";
                    sendto(serverSocket, response.c_str(), response.length(), 0,
                        (sockaddr*)&clientAddr, clientAddrLen);
                    continue;
                }

                playerUUID = generateUUID();
                PlayerData newPlayer;
                newPlayer.uuid = playerUUID;
                newPlayer.name = playerName;
                respawnPlayer(newPlayer);
                newPlayer.lastSeenIP = clientIP;
                newPlayer.lastSeenPort = clientPort;
                newPlayer.lastPingResponse = std::chrono::steady_clock::now();
                newPlayer.lastMovement = std::chrono::steady_clock::now();
                newPlayer.lastPingSent = std::chrono::steady_clock::now();
                newPlayer.lastSplit = std::chrono::steady_clock::now();
                newPlayer.lastMerge = std::chrono::steady_clock::now();
                players[playerUUID] = newPlayer;
                std::cout << "[NEW] " << playerName << " joined (" << players.size() << "/" << MAX_PLAYERS << ")" << std::endl;

                // Update server finder with new player count
                registerWithServerFinder(players.size());
            }

            PlayerData& player = players[playerUUID];
            player.lastPingResponse = std::chrono::steady_clock::now();

            float avgX = 0, avgY = 0;
            for (const auto& cell : player.cells) {
                avgX += cell.x;
                avgY += cell.y;
            }
            avgX /= player.cells.size();
            avgY /= player.cells.size();

            float viewDistance = 300.0f;

            response = "UUID:" + playerUUID +
                "|MAP:" + std::to_string(MAP_WIDTH) + "," + std::to_string(MAP_HEIGHT) +
                "|POS:" + std::to_string(avgX) + "," + std::to_string(avgY) +
                "|SIZE:" + std::to_string(player.cells[0].size) +
                "|COLOR:" + std::to_string((int)player.colorR) + "," +
                std::to_string((int)player.colorG) + "," +
                std::to_string((int)player.colorB) +
                "|" + buildPlayerList(players) +
                "|" + buildNearbyFoodList(food, avgX, avgY, viewDistance);

            sendto(serverSocket, response.c_str(), response.length(), 0,
                (sockaddr*)&clientAddr, clientAddrLen);
            continue;
        }
        else {
            playerUUID = receivedUUID;
            if (players.find(playerUUID) == players.end()) continue;
            players[playerUUID].lastSeenIP = clientIP;
            players[playerUUID].lastSeenPort = clientPort;
            players[playerUUID].lastPingResponse = std::chrono::steady_clock::now();
        }

        PlayerData& player = players[playerUUID];

        if (command == "ACK" || command == "PONG") {
            continue;
        }

        if (command == "SPLIT") {
            splitPlayer(player);
            continue;
        }

        if (command == "MERGE") {
            mergePlayer(player);
            continue;
        }

        float moveX = 0.0f, moveY = 0.0f;
        std::stringstream commandStream(command);
        std::string singleCommand;

        while (std::getline(commandStream, singleCommand, ',')) {
            if (singleCommand == "UP") moveY += 1.0f;
            else if (singleCommand == "DOWN") moveY -= 1.0f;
            else if (singleCommand == "LEFT") moveX -= 1.0f;
            else if (singleCommand == "RIGHT") moveX += 1.0f;
        }

        if (moveX != 0.0f || moveY != 0.0f) {
            player.lastMovement = std::chrono::steady_clock::now();

            if (moveX != 0.0f && moveY != 0.0f) {
                float length = sqrt(moveX * moveX + moveY * moveY);
                moveX /= length;
                moveY /= length;
            }

            for (auto& cell : player.cells) {
                float speed = MOVE_SPEED_BASE * (PLAYER_START_SIZE / cell.size);
                float cellMoveX = moveX * speed;
                float cellMoveY = moveY * speed;

                float newX = cell.x + cellMoveX;
                float newY = cell.y + cellMoveY;

                if (newX < cell.size) newX = cell.size;
                if (newX >= MAP_WIDTH - cell.size) newX = MAP_WIDTH - cell.size;
                if (newY < cell.size) newY = cell.size;
                if (newY >= MAP_HEIGHT - cell.size) newY = MAP_HEIGHT - cell.size;

                cell.x = newX;
                cell.y = newY;
            }
        }

        for (auto& cell : player.cells) {
            auto foodIt = food.begin();
            while (foodIt != food.end()) {
                if (checkCollision(cell.x, cell.y, cell.size, foodIt->x, foodIt->y, FOOD_SIZE)) {
                    float growth = FOOD_SIZE * GROWTH_RATE_FOOD;
                    cell.size += growth;
                    if (cell.size > MAX_PLAYER_SIZE) cell.size = MAX_PLAYER_SIZE;
                    foodIt = food.erase(foodIt);
                }
                else {
                    ++foodIt;
                }
            }
        }

        for (auto& otherPair : players) {
            if (otherPair.first == playerUUID) {
                for (size_t i = 0; i < player.cells.size(); i++) {
                    for (size_t j = i + 1; j < player.cells.size(); j++) {
                        if (isCompleteOverlap(player.cells[i].x, player.cells[i].y, player.cells[i].size,
                            player.cells[j].x, player.cells[j].y, player.cells[j].size)) {
                            float newSize = sqrt(player.cells[i].size * player.cells[i].size +
                                player.cells[j].size * player.cells[j].size);
                            player.cells[i].size = newSize;
                            player.cells[i].x = (player.cells[i].x + player.cells[j].x) / 2;
                            player.cells[i].y = (player.cells[i].y + player.cells[j].y) / 2;
                            player.cells.erase(player.cells.begin() + j);
                            j--;
                        }
                    }
                }
                continue;
            }

            PlayerData& other = otherPair.second;

            for (auto& cell : player.cells) {
                auto otherCellIt = other.cells.begin();
                while (otherCellIt != other.cells.end()) {
                    if (cell.size > otherCellIt->size * 1.1f) {
                        if (isCompleteOverlap(cell.x, cell.y, cell.size,
                            otherCellIt->x, otherCellIt->y, otherCellIt->size)) {
                            float growth = otherCellIt->size * GROWTH_RATE_PLAYER;
                            cell.size += growth;
                            if (cell.size > MAX_PLAYER_SIZE) cell.size = MAX_PLAYER_SIZE;

                            otherCellIt = other.cells.erase(otherCellIt);

                            if (other.cells.empty()) {
                                std::cout << "[EAT] " << player.name << " ate " << other.name << std::endl;
                                respawnPlayer(other);
                                other.lastMovement = std::chrono::steady_clock::now();
                                break;
                            }
                        }
                        else {
                            ++otherCellIt;
                        }
                    }
                    else {
                        ++otherCellIt;
                    }
                }
            }
        }

        float avgX = 0, avgY = 0;
        for (const auto& cell : player.cells) {
            avgX += cell.x;
            avgY += cell.y;
        }
        avgX /= player.cells.size();
        avgY /= player.cells.size();

        float viewDistance = 300.0f;
        response = "POS:" + std::to_string(avgX) + "," + std::to_string(avgY) +
            "|SIZE:" + std::to_string(player.cells[0].size) +
            "|" + buildPlayerList(players) +
            "|" + buildNearbyFoodList(food, avgX, avgY, viewDistance);

        sendto(serverSocket, response.c_str(), response.length(), 0,
            (sockaddr*)&clientAddr, clientAddrLen);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}