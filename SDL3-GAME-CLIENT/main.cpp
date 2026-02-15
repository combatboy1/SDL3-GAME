#define NOMINMAX
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "network_common.h"
#include "server_browser.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "SDL3.lib")
#pragma comment(lib, "SDL3_ttf.lib")

int WINDOW_WIDTH = 1280;
int WINDOW_HEIGHT = 720;

const float MINIMAP_SIZE_RATIO = 0.15f;
const int MINIMAP_MARGIN = 10;
const int MINIMAP_BORDER = 3;

const float WORLD_TO_PIXEL_SCALE = 2.0f;

int MAP_WIDTH = 200;
int MAP_HEIGHT = 200;

enum GameState {
    STATE_BROWSER,
    STATE_MENU,
    STATE_CONNECTING,
    STATE_PLAYING
};

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
    std::string name;
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
};

struct Player {
    std::string uuid;
    std::string name;
    std::vector<Cell> cells;
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
};

struct AppState {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* fontLarge = nullptr;
    TTF_Font* fontMedium = nullptr;
    TTF_Font* fontSmall = nullptr;
    SOCKET clientSocket = INVALID_SOCKET;
    sockaddr_in6 serverAddr;

    GameState gameState = STATE_BROWSER;
    BrowserContext browser;
    bool returnToBrowser = false;

    std::string serverIP = "::1";
    std::string playerName = "";
    std::string inputBuffer = "";
    bool editingServer = false;
    bool editingName = false;
    std::string errorMessage = "";

    std::string assignedUUID;
    std::vector<Cell> myCells;
    uint8_t myColorR = 100;
    uint8_t myColorG = 100;
    uint8_t myColorB = 255;
    std::map<std::string, Player> otherPlayers;
    std::vector<FoodDot> food;
    bool running = true;
    Uint64 lastInputTime = 0;
    const Uint64 INPUT_COOLDOWN = 50;

    bool keyW = false;
    bool keyA = false;
    bool keyS = false;
    bool keyD = false;
};

inline float clamp_float(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

void drawText(AppState* state, TTF_Font* font, const std::string& text, float x, float y, SDL_Color color) {
    if (text.empty() || !font) return;

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.length(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(state->renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect destRect = { x, y, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(state->renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void drawTextCentered(AppState* state, TTF_Font* font, const std::string& text, float x, float y, SDL_Color color) {
    if (text.empty() || !font) return;

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.length(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(state->renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect destRect = { x - surface->w / 2.0f, y - surface->h / 2.0f, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(state->renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void drawTextInCircle(AppState* state, const std::string& text, float x, float y, float maxWidth) {
    if (text.empty() || !state->fontMedium) return;

    SDL_Color color = { 255, 255, 255, 255 };
    SDL_Surface* surface = TTF_RenderText_Blended(state->fontMedium, text.c_str(), text.length(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(state->renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    float textW = surface->w;
    float textH = surface->h;

    // Scale text to fit in circle, but enforce minimum size
    if (textW > maxWidth) {
        float scale = maxWidth / textW;
        textW *= scale;
        textH *= scale;
    }

    // Minimum text size to remain readable
    const float MIN_TEXT_HEIGHT = 12.0f;
    if (textH < MIN_TEXT_HEIGHT) {
        float scale = MIN_TEXT_HEIGHT / textH;
        textW *= scale;
        textH *= scale;
    }

    SDL_FRect destRect = { x - textW / 2, y - textH / 2, textW, textH };
    SDL_RenderTexture(state->renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

void drawInputBox(AppState* state, const std::string& label, const std::string& value,
    float x, float y, float w, float h, bool active) {
    SDL_SetRenderDrawColor(state->renderer, 40, 40, 40, 255);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(state->renderer, &rect);

    if (active) {
        SDL_SetRenderDrawColor(state->renderer, 100, 150, 255, 255);
    }
    else {
        SDL_SetRenderDrawColor(state->renderer, 150, 150, 150, 255);
    }
    SDL_RenderRect(state->renderer, &rect);

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color gray = { 180, 180, 180, 255 };

    drawText(state, state->fontSmall, label, x, y - 25, gray);

    std::string displayText = value;
    if (active) displayText += "_";

    drawText(state, state->fontMedium, displayText, x + 10, y + h / 2 - 12, white);
}

void drawButton(AppState* state, const std::string& text, float x, float y, float w, float h) {
    SDL_SetRenderDrawColor(state->renderer, 70, 120, 200, 255);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(state->renderer, &rect);

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderRect(state->renderer, &rect);

    SDL_Color white = { 255, 255, 255, 255 };
    drawTextCentered(state, state->fontMedium, text, x + w / 2, y + h / 2, white);
}

void drawMenu(AppState* state) {
    SDL_SetRenderDrawColor(state->renderer, 30, 30, 50, 255);
    SDL_RenderClear(state->renderer);

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color red = { 255, 100, 100, 255 };
    SDL_Color cyan = { 100, 255, 255, 255 };
    SDL_Color gray = { 180, 180, 180, 255 };

    drawTextCentered(state, state->fontLarge, "Direct Connect", WINDOW_WIDTH / 2, 100, white);

    float centerX = WINDOW_WIDTH / 2;
    float centerY = WINDOW_HEIGHT / 2;

    drawInputBox(state, "Server Address:", state->editingServer ? state->inputBuffer : state->serverIP,
        centerX - 200, centerY - 150, 400, 50, state->editingServer);

    drawInputBox(state, "Your Name:", state->editingName ? state->inputBuffer : state->playerName,
        centerX - 200, centerY - 50, 400, 50, state->editingName);

    // NEW: Server Code input box
    drawInputBox(state, "Server Code (optional):", state->browser.editingCode ? state->inputBuffer : state->browser.codeInput,
        centerX - 200, centerY + 50, 400, 50, state->browser.editingCode);

    if (!state->playerName.empty()) {
        drawButton(state, "CONNECT", centerX - 100, centerY + 150, 200, 60);
    }
    else {
        SDL_SetRenderDrawColor(state->renderer, 100, 100, 100, 255);
        SDL_FRect rect = { centerX - 100, centerY + 150, 200, 60 };
        SDL_RenderFillRect(state->renderer, &rect);
        drawTextCentered(state, state->fontMedium, "CONNECT", centerX, centerY + 180, { 150, 150, 150, 255 });
    }

    if (!state->errorMessage.empty()) {
        drawTextCentered(state, state->fontSmall, state->errorMessage, centerX, centerY + 250, red);
    }

    drawTextCentered(state, state->fontSmall, "Press ESC to go back to browser",
        centerX, WINDOW_HEIGHT - 60, gray);
    drawTextCentered(state, state->fontSmall, "LEFT CLICK: Split | RIGHT CLICK: Merge",
        centerX, WINDOW_HEIGHT - 30, cyan);
}

void drawConnecting(AppState* state) {
    SDL_SetRenderDrawColor(state->renderer, 30, 30, 50, 255);
    SDL_RenderClear(state->renderer);

    SDL_Color white = { 255, 255, 255, 255 };
    drawTextCentered(state, state->fontLarge, "Connecting...", WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2, white);
}

void sendCommand(AppState* state, const std::string& command) {
    std::string message = state->assignedUUID + ":" + state->playerName + ":" + command;
    sendto(state->clientSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
}

void sendSplit(AppState* state) {
    std::string message = state->assignedUUID + ":" + state->playerName + ":SPLIT";
    sendto(state->clientSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
}

void sendMerge(AppState* state) {
    std::string message = state->assignedUUID + ":" + state->playerName + ":MERGE";
    sendto(state->clientSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
}

void sendPong(AppState* state) {
    std::string message = state->assignedUUID + ":" + state->playerName + ":PONG";
    sendto(state->clientSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
}

void sendAck(AppState* state) {
    std::string message = state->assignedUUID + ":" + state->playerName + ":ACK";
    sendto(state->clientSocket, message.c_str(), message.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
}

void parseServerResponse(AppState* state, const std::string& response) {
    if (response == "PING") {
        sendPong(state);
        return;
    }

    std::stringstream ss(response);
    std::string token;
    bool receivedData = false;

    while (std::getline(ss, token, '|')) {
        if (token.substr(0, 5) == "UUID:") {
            state->assignedUUID = token.substr(5);
        }
        else if (token.substr(0, 4) == "MAP:") {
            std::string mapData = token.substr(4);
            size_t commaPos = mapData.find(',');
            if (commaPos != std::string::npos) {
                MAP_WIDTH = std::stoi(mapData.substr(0, commaPos));
                MAP_HEIGHT = std::stoi(mapData.substr(commaPos + 1));
            }
        }
        else if (token.substr(0, 6) == "COLOR:") {
            std::string colorData = token.substr(6);
            std::stringstream colorStream(colorData);
            std::string rStr, gStr, bStr;
            std::getline(colorStream, rStr, ',');
            std::getline(colorStream, gStr, ',');
            std::getline(colorStream, bStr, ',');
            state->myColorR = std::stoi(rStr);
            state->myColorG = std::stoi(gStr);
            state->myColorB = std::stoi(bStr);
        }
        else if (token.substr(0, 8) == "PLAYERS:") {
            state->myCells.clear();
            state->otherPlayers.clear();

            std::string playersData = token.substr(8);
            if (!playersData.empty()) {
                std::stringstream playerStream(playersData);
                std::string playerToken;
                while (std::getline(playerStream, playerToken, ';')) {
                    if (playerToken.empty()) continue;
                    std::stringstream playerInfo(playerToken);
                    std::string uuid, name, xStr, yStr, sizeStr, rStr, gStr, bStr;
                    std::getline(playerInfo, uuid, ',');
                    std::getline(playerInfo, name, ',');
                    std::getline(playerInfo, xStr, ',');
                    std::getline(playerInfo, yStr, ',');
                    std::getline(playerInfo, sizeStr, ',');
                    std::getline(playerInfo, rStr, ',');
                    std::getline(playerInfo, gStr, ',');
                    std::getline(playerInfo, bStr, ',');

                    Cell cell;
                    cell.x = std::stof(xStr);
                    cell.y = std::stof(yStr);
                    cell.size = std::stof(sizeStr);
                    cell.name = name;
                    cell.colorR = std::stoi(rStr);
                    cell.colorG = std::stoi(gStr);
                    cell.colorB = std::stoi(bStr);

                    if (uuid == state->assignedUUID) {
                        state->myCells.push_back(cell);
                    }
                    else {
                        if (state->otherPlayers.find(uuid) == state->otherPlayers.end()) {
                            Player p;
                            p.uuid = uuid;
                            p.name = name;
                            p.colorR = cell.colorR;
                            p.colorG = cell.colorG;
                            p.colorB = cell.colorB;
                            state->otherPlayers[uuid] = p;
                        }
                        state->otherPlayers[uuid].cells.push_back(cell);
                    }
                }
            }
            receivedData = true;
        }
        else if (token.substr(0, 5) == "FOOD:") {
            state->food.clear();
            std::string foodData = token.substr(5);
            if (!foodData.empty()) {
                std::stringstream foodStream(foodData);
                std::string foodToken;
                while (std::getline(foodStream, foodToken, ';')) {
                    if (foodToken.empty()) continue;
                    std::stringstream foodInfo(foodToken);
                    std::string idStr, xStr, yStr, rStr, gStr, bStr;
                    std::getline(foodInfo, idStr, ',');
                    std::getline(foodInfo, xStr, ',');
                    std::getline(foodInfo, yStr, ',');
                    std::getline(foodInfo, rStr, ',');
                    std::getline(foodInfo, gStr, ',');
                    std::getline(foodInfo, bStr, ',');
                    FoodDot f;
                    f.id = std::stoi(idStr);
                    f.x = std::stof(xStr);
                    f.y = std::stof(yStr);
                    f.r = std::stoi(rStr);
                    f.g = std::stoi(gStr);
                    f.b = std::stoi(bStr);
                    state->food.push_back(f);
                }
            }
            receivedData = true;
        }
    }

    if (receivedData) sendAck(state);
}

void checkServerMessages(AppState* state) {
    char buffer[32768];
    int serverAddrLen = sizeof(state->serverAddr);
    int recvLen = recvfrom(state->clientSocket, buffer, sizeof(buffer) - 1, 0,
        (sockaddr*)&state->serverAddr, &serverAddrLen);
    if (recvLen > 0) {
        buffer[recvLen] = '\0';
        parseServerResponse(state, std::string(buffer));
    }
}

void processHeldKeys(AppState* state) {
    Uint64 currentTime = SDL_GetTicks();
    if (currentTime - state->lastInputTime < state->INPUT_COOLDOWN) return;

    std::string command = "";
    if (state->keyW) command += "DOWN";
    if (state->keyS) { if (!command.empty()) command += ","; command += "UP"; }
    if (state->keyA) { if (!command.empty()) command += ","; command += "LEFT"; }
    if (state->keyD) { if (!command.empty()) command += ","; command += "RIGHT"; }

    if (!command.empty()) {
        sendCommand(state, command);
        state->lastInputTime = currentTime;
    }
}

float worldToScreenX(AppState* state, float worldX) {
    float avgX = 0;
    for (const auto& cell : state->myCells) {
        avgX += cell.x;
    }
    if (!state->myCells.empty()) avgX /= state->myCells.size();

    return (WINDOW_WIDTH / 2.0f) + ((worldX - avgX) * WORLD_TO_PIXEL_SCALE);
}

float worldToScreenY(AppState* state, float worldY) {
    float avgY = 0;
    for (const auto& cell : state->myCells) {
        avgY += cell.y;
    }
    if (!state->myCells.empty()) avgY /= state->myCells.size();

    return (WINDOW_HEIGHT / 2.0f) + ((worldY - avgY) * WORLD_TO_PIXEL_SCALE);
}

float worldToPixelSize(float worldSize) {
    return worldSize * WORLD_TO_PIXEL_SCALE;
}

void drawGrid(AppState* state) {
    SDL_SetRenderDrawColor(state->renderer, 240, 240, 240, 255);
    SDL_FRect bgRect = { 0, 0, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT };
    SDL_RenderFillRect(state->renderer, &bgRect);

    float avgX = 0, avgY = 0;
    for (const auto& cell : state->myCells) {
        avgX += cell.x;
        avgY += cell.y;
    }
    if (!state->myCells.empty()) {
        avgX /= state->myCells.size();
        avgY /= state->myCells.size();
    }

    float worldLeft = avgX - (WINDOW_WIDTH / (2.0f * WORLD_TO_PIXEL_SCALE));
    float worldTop = avgY - (WINDOW_HEIGHT / (2.0f * WORLD_TO_PIXEL_SCALE));
    const float GRID_SIZE = 25.0f;

    SDL_SetRenderDrawColor(state->renderer, 220, 220, 220, 255);

    int firstCol = (int)floor(worldLeft / GRID_SIZE);
    int lastCol = (int)ceil((worldLeft + WINDOW_WIDTH / WORLD_TO_PIXEL_SCALE) / GRID_SIZE);
    int firstRow = (int)floor(worldTop / GRID_SIZE);
    int lastRow = (int)ceil((worldTop + WINDOW_HEIGHT / WORLD_TO_PIXEL_SCALE) / GRID_SIZE);

    for (int col = firstCol; col <= lastCol; col++) {
        float worldX = col * GRID_SIZE;
        float screenX = worldToScreenX(state, worldX);
        SDL_RenderLine(state->renderer, screenX, 0, screenX, (float)WINDOW_HEIGHT);
    }

    for (int row = firstRow; row <= lastRow; row++) {
        float worldY = row * GRID_SIZE;
        float screenY = worldToScreenY(state, worldY);
        SDL_RenderLine(state->renderer, 0, screenY, (float)WINDOW_WIDTH, screenY);
    }

    SDL_SetRenderDrawColor(state->renderer, 255, 0, 0, 255);
    float mapLeft = worldToScreenX(state, 0);
    float mapRight = worldToScreenX(state, (float)MAP_WIDTH);
    float mapTop = worldToScreenY(state, 0);
    float mapBottom = worldToScreenY(state, (float)MAP_HEIGHT);

    for (int i = 0; i < 4; i++) {
        SDL_RenderLine(state->renderer, mapLeft + i, clamp_float(mapTop, 0, (float)WINDOW_HEIGHT),
            mapLeft + i, clamp_float(mapBottom, 0, (float)WINDOW_HEIGHT));
        SDL_RenderLine(state->renderer, mapRight - i, clamp_float(mapTop, 0, (float)WINDOW_HEIGHT),
            mapRight - i, clamp_float(mapBottom, 0, (float)WINDOW_HEIGHT));
        SDL_RenderLine(state->renderer, clamp_float(mapLeft, 0, (float)WINDOW_WIDTH), mapTop + i,
            clamp_float(mapRight, 0, (float)WINDOW_WIDTH), mapTop + i);
        SDL_RenderLine(state->renderer, clamp_float(mapLeft, 0, (float)WINDOW_WIDTH), mapBottom - i,
            clamp_float(mapRight, 0, (float)WINDOW_WIDTH), mapBottom - i);
    }
}

void drawFood(AppState* state) {
    for (const auto& f : state->food) {
        float screenX = worldToScreenX(state, f.x);
        float screenY = worldToScreenY(state, f.y);
        float pixelSize = worldToPixelSize(5.0f);

        if (screenX < -pixelSize || screenX > WINDOW_WIDTH + pixelSize ||
            screenY < -pixelSize || screenY > WINDOW_HEIGHT + pixelSize) continue;

        SDL_SetRenderDrawColor(state->renderer, f.r, f.g, f.b, 255);

        int radius = (int)pixelSize;
        for (int y = -radius; y <= radius; y++) {
            int width = (int)sqrt(radius * radius - y * y);
            SDL_RenderLine(state->renderer, screenX - width, screenY + y, screenX + width, screenY + y);
        }
    }
}

void fillCircle(SDL_Renderer* renderer, float cx, float cy, float radius) {
    int r = (int)radius;
    for (int y = -r; y <= r; y++) {
        int width = (int)sqrt(r * r - y * y);
        SDL_RenderLine(renderer, cx - width, cy + y, cx + width, cy + y);
    }
}

void drawCell(AppState* state, const Cell& cell, bool isPlayer) {
    float screenX = worldToScreenX(state, cell.x);
    float screenY = worldToScreenY(state, cell.y);
    float pixelSize = worldToPixelSize(cell.size);

    if (screenX < -pixelSize || screenX > WINDOW_WIDTH + pixelSize ||
        screenY < -pixelSize || screenY > WINDOW_HEIGHT + pixelSize) return;

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 100);
    fillCircle(state->renderer, screenX + 3, screenY + 3, pixelSize);

    SDL_SetRenderDrawColor(state->renderer, cell.colorR, cell.colorG, cell.colorB, 255);
    fillCircle(state->renderer, screenX, screenY, pixelSize);

    drawTextInCircle(state, cell.name, screenX, screenY, pixelSize * 1.8f);
}

void drawLeaderboard(AppState* state) {
    std::vector<std::pair<std::string, float>> leaderboard;

    float myTotalSize = 0;
    for (const auto& cell : state->myCells) {
        myTotalSize += cell.size;
    }
    leaderboard.push_back({ state->playerName, myTotalSize });

    for (const auto& pair : state->otherPlayers) {
        float totalSize = 0;
        for (const auto& cell : pair.second.cells) {
            totalSize += cell.size;
        }
        leaderboard.push_back({ pair.second.name, totalSize });
    }

    std::sort(leaderboard.begin(), leaderboard.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    float lbX = WINDOW_WIDTH - 220;
    float lbY = 10;
    float lbWidth = 210;
    float lbHeight = 30 + (std::min((int)leaderboard.size(), 10) * 30);

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 150);
    SDL_FRect bgRect = { lbX, lbY, lbWidth, lbHeight };
    SDL_RenderFillRect(state->renderer, &bgRect);
    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderRect(state->renderer, &bgRect);

    if (!state->fontSmall) return;
    SDL_Color white = { 255, 255, 255, 255 };
    drawText(state, state->fontSmall, "Leaderboard", lbX + 10, lbY + 5, white);

    for (int i = 0; i < std::min((int)leaderboard.size(), 10); i++) {
        std::string entry = std::to_string(i + 1) + ". " + leaderboard[i].first;
        drawText(state, state->fontSmall, entry, lbX + 10, lbY + 30 + i * 30, white);
    }
}

void drawMinimap(AppState* state) {
    int minimapSize = (int)(WINDOW_HEIGHT * MINIMAP_SIZE_RATIO);
    if (minimapSize < 100) minimapSize = 100;
    if (minimapSize > 200) minimapSize = 200;

    float minimapX = 10;
    float minimapY = WINDOW_HEIGHT - minimapSize - 10;

    SDL_SetRenderDrawColor(state->renderer, 40, 40, 40, 200);
    SDL_FRect bgRect = { minimapX - 3, minimapY - 3, (float)(minimapSize + 6), (float)(minimapSize + 6) };
    SDL_RenderFillRect(state->renderer, &bgRect);

    SDL_SetRenderDrawColor(state->renderer, 220, 220, 220, 255);
    SDL_FRect mapRect = { minimapX, minimapY, (float)minimapSize, (float)minimapSize };
    SDL_RenderFillRect(state->renderer, &mapRect);

    float scaleX = (float)minimapSize / (float)MAP_WIDTH;
    float scaleY = (float)minimapSize / (float)MAP_HEIGHT;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    for (const auto& pair : state->otherPlayers) {
        for (const auto& cell : pair.second.cells) {
            float dotX = minimapX + (cell.x * scale);
            float dotY = minimapY + (cell.y * scale);
            SDL_SetRenderDrawColor(state->renderer, pair.second.colorR, pair.second.colorG, pair.second.colorB, 255);
            SDL_FRect dotRect = { dotX - 2, dotY - 2, 4, 4 };
            SDL_RenderFillRect(state->renderer, &dotRect);
        }
    }

    for (const auto& cell : state->myCells) {
        float dotX = minimapX + (cell.x * scale);
        float dotY = minimapY + (cell.y * scale);
        SDL_SetRenderDrawColor(state->renderer, state->myColorR, state->myColorG, state->myColorB, 255);
        SDL_FRect dotRect = { dotX - 3, dotY - 3, 6, 6 };
        SDL_RenderFillRect(state->renderer, &dotRect);
    }

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderRect(state->renderer, &bgRect);
}

void drawCellCount(AppState* state) {
    if (state->myCells.empty()) return;

    std::string cellInfo = "Cells: " + std::to_string(state->myCells.size());
    SDL_Color white = { 255, 255, 255, 255 };
    drawText(state, state->fontMedium, cellInfo, 10, 10, white);
}

bool connectToServer(AppState* state) {
    if (state->clientSocket != INVALID_SOCKET) {
        closesocket(state->clientSocket);
    }

    state->clientSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (state->clientSocket == INVALID_SOCKET) {
        state->errorMessage = "Failed to create socket";
        return false;
    }

    int bufferSize = 65536;
    setsockopt(state->clientSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));

    memset(&state->serverAddr, 0, sizeof(state->serverAddr));
    state->serverAddr.sin6_family = AF_INET6;
    state->serverAddr.sin6_port = htons(8888);

    if (inet_pton(AF_INET6, state->serverIP.c_str(), &state->serverAddr.sin6_addr) != 1) {
        state->errorMessage = "Invalid server address";
        closesocket(state->clientSocket);
        state->clientSocket = INVALID_SOCKET;
        return false;
    }

    std::string initMessage = "NONE:" + state->playerName + ":INIT";
    sendto(state->clientSocket, initMessage.c_str(), initMessage.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));

    u_long blockingMode = 0;
    ioctlsocket(state->clientSocket, FIONBIO, &blockingMode);

    char buffer[32768];
    int serverAddrLen = sizeof(state->serverAddr);

    for (int attempt = 0; attempt < 3; attempt++) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(state->clientSocket, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        int result = select(0, &readfds, NULL, NULL, &timeout);

        if (result > 0) {
            int recvLen = recvfrom(state->clientSocket, buffer, sizeof(buffer) - 1, 0,
                (sockaddr*)&state->serverAddr, &serverAddrLen);

            if (recvLen > 0) {
                buffer[recvLen] = '\0';
                std::string response(buffer);

                if (response.substr(0, 6) == "ERROR:") {
                    std::string errorType = response.substr(6);
                    if (errorType == "CODE_REQUIRED") {
                        state->errorMessage = "Server requires a code";
                    }
                    else if (errorType == "WRONG_CODE") {
                        state->errorMessage = "Incorrect server code";
                    }
                    else if (errorType == "SERVER_FULL") {
                        state->errorMessage = "Server is full";
                    }
                    else {
                        state->errorMessage = "Connection error";
                    }
                    closesocket(state->clientSocket);
                    state->clientSocket = INVALID_SOCKET;
                    return false;
                }

                parseServerResponse(state, response);

                if (!state->assignedUUID.empty()) {
                    u_long mode = 1;
                    ioctlsocket(state->clientSocket, FIONBIO, &mode);
                    return true;
                }
            }
        }

        if (attempt < 2) {
            sendto(state->clientSocket, initMessage.c_str(), initMessage.length(), 0,
                (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
        }
    }

    state->errorMessage = "Connection timeout";
    closesocket(state->clientSocket);
    state->clientSocket = INVALID_SOCKET;
    return false;
}

bool connectToServerWithCode(AppState* state, const std::string& serverIP, int port, const std::string& serverCode) {
    if (state->clientSocket != INVALID_SOCKET) {
        closesocket(state->clientSocket);
    }

    state->clientSocket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (state->clientSocket == INVALID_SOCKET) {
        state->errorMessage = "Failed to create socket";
        return false;
    }

    int bufferSize = 65536;
    setsockopt(state->clientSocket, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));

    memset(&state->serverAddr, 0, sizeof(state->serverAddr));
    state->serverAddr.sin6_family = AF_INET6;
    state->serverAddr.sin6_port = htons(port);

    if (inet_pton(AF_INET6, serverIP.c_str(), &state->serverAddr.sin6_addr) != 1) {
        state->errorMessage = "Invalid server address";
        closesocket(state->clientSocket);
        state->clientSocket = INVALID_SOCKET;
        return false;
    }

    std::string initMessage = serverCode.empty() ?
        "NONE:" + state->playerName + ":INIT" :
        "NONE:" + state->playerName + ":CODE:" + serverCode;

    sendto(state->clientSocket, initMessage.c_str(), initMessage.length(), 0,
        (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));

    u_long blockingMode = 0;
    ioctlsocket(state->clientSocket, FIONBIO, &blockingMode);

    char buffer[32768];
    int serverAddrLen = sizeof(state->serverAddr);

    for (int attempt = 0; attempt < 3; attempt++) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(state->clientSocket, &readfds);

        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        int result = select(0, &readfds, NULL, NULL, &timeout);

        if (result > 0) {
            int recvLen = recvfrom(state->clientSocket, buffer, sizeof(buffer) - 1, 0,
                (sockaddr*)&state->serverAddr, &serverAddrLen);

            if (recvLen > 0) {
                buffer[recvLen] = '\0';
                std::string response(buffer);

                if (response.substr(0, 6) == "ERROR:") {
                    std::string errorType = response.substr(6);
                    if (errorType == "CODE_REQUIRED") {
                        state->errorMessage = "Server requires a code";
                    }
                    else if (errorType == "WRONG_CODE") {
                        state->errorMessage = "Incorrect server code";
                    }
                    else if (errorType == "SERVER_FULL") {
                        state->errorMessage = "Server is full";
                    }
                    else {
                        state->errorMessage = "Connection error";
                    }
                    closesocket(state->clientSocket);
                    state->clientSocket = INVALID_SOCKET;
                    return false;
                }

                parseServerResponse(state, response);

                if (!state->assignedUUID.empty()) {
                    u_long mode = 1;
                    ioctlsocket(state->clientSocket, FIONBIO, &mode);
                    return true;
                }
            }
        }

        if (attempt < 2) {
            sendto(state->clientSocket, initMessage.c_str(), initMessage.length(), 0,
                (sockaddr*)&state->serverAddr, sizeof(state->serverAddr));
        }
    }

    state->errorMessage = "Connection timeout";
    closesocket(state->clientSocket);
    state->clientSocket = INVALID_SOCKET;
    return false;
}

int main(int argc, char* argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        WSACleanup();
        return 1;
    }

    if (TTF_Init() < 0) {
        SDL_Quit();
        WSACleanup();
        return 1;
    }

    AppState state;

    state.window = SDL_CreateWindow("Blob Game",
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);

    if (!state.window) {
        TTF_Quit();
        SDL_Quit();
        WSACleanup();
        return 1;
    }

    SDL_GetWindowSize(state.window, &WINDOW_WIDTH, &WINDOW_HEIGHT);

    state.renderer = SDL_CreateRenderer(state.window, nullptr);
    if (!state.renderer) {
        SDL_DestroyWindow(state.window);
        TTF_Quit();
        SDL_Quit();
        WSACleanup();
        return 1;
    }

    state.fontLarge = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 48);
    state.fontMedium = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 24);
    state.fontSmall = TTF_OpenFont("C:\\Windows\\Fonts\\arial.ttf", 18);

    SDL_Event event;
    while (state.running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                state.running = false;
            }

            if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                WINDOW_WIDTH = event.window.data1;
                WINDOW_HEIGHT = event.window.data2;
            }

            if (state.gameState == STATE_BROWSER) {
                // Handle mouse/scroll input FIRST (before text input)
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    event.type == SDL_EVENT_MOUSE_WHEEL) {
                    handleBrowserInput(state.browser, event, WINDOW_WIDTH, WINDOW_HEIGHT);
                }

                // Handle text input for browser
                if (event.type == SDL_EVENT_TEXT_INPUT) {
                    if (state.browser.editingSearch) {
                        state.browser.searchQuery += event.text.text;
                    }
                    else if (state.browser.editingCode) {
                        state.browser.codeInput += event.text.text;
                    }
                    else if (state.browser.editingName) {
                        state.browser.nameInput += event.text.text;
                    }
                }

                // Handle keyboard input
                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_BACKSPACE) {
                        if (state.browser.editingSearch && !state.browser.searchQuery.empty()) {
                            state.browser.searchQuery.pop_back();
                        }
                        else if (state.browser.editingCode && !state.browser.codeInput.empty()) {
                            state.browser.codeInput.pop_back();
                        }
                        else if (state.browser.editingName && !state.browser.nameInput.empty()) {
                            state.browser.nameInput.pop_back();
                        }
                    }
                    else if (event.key.key == SDLK_RETURN) {
                        state.browser.editingSearch = false;
                        state.browser.editingCode = false;
                        state.browser.editingName = false;  // ADD THIS LINE
                    }

                    else if (event.key.key == SDLK_ESCAPE) {
                        if (state.browser.state == BROWSER_SERVER_LIST) {
                            state.browser.state = BROWSER_MAIN_MENU;
                            state.browser.selectedServer = -1;
                            state.browser.searchQuery = "";
                            state.browser.codeInput = "";
                            state.browser.nameInput = "";  // ADD THIS LINE
                        }
                        state.browser.editingSearch = false;
                        state.browser.editingCode = false;
                        state.browser.editingName = false;  // ADD THIS LINE
                    }
                }

                // Enable/disable text input based on editing state
                // Do this AFTER handling mouse clicks
                static bool textInputActive = false;
                if ((state.browser.editingSearch || state.browser.editingCode || state.browser.editingName) && !textInputActive) {
                    SDL_StartTextInput(state.window);
                    textInputActive = true;
                }
                else if (!state.browser.editingSearch && !state.browser.editingCode && !state.browser.editingName && textInputActive) {
                    SDL_StopTextInput(state.window);
                    textInputActive = false;
                }

                if (state.browser.state == BROWSER_DIRECT_CONNECT) {
                    state.gameState = STATE_MENU;
                    state.returnToBrowser = false;
                }

                if (state.browser.state == BROWSER_SERVER_LIST &&
                    state.browser.selectedServer >= 0 &&
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {

                    float mx = event.button.x;
                    float my = event.button.y;

                    std::vector<ServerInfo> filteredServers;
                    for (const auto& server : state.browser.servers) {
                        if (state.browser.searchQuery.empty() ||
                            server.name.find(state.browser.searchQuery) != std::string::npos) {
                            filteredServers.push_back(server);
                        }
                    }

                    if (state.browser.selectedServer < (int)filteredServers.size()) {
                        float y = 220 + (state.browser.selectedServer - state.browser.scrollOffset) * 70;
                        if (mx >= WINDOW_WIDTH - 180 && mx <= WINDOW_WIDTH - 80 &&
                            my >= y + 15 && my <= y + 50) {

                            const ServerInfo& server = filteredServers[state.browser.selectedServer];
                            state.serverIP = server.address;
                            state.playerName = state.browser.nameInput;
                            state.gameState = STATE_CONNECTING;
                            state.returnToBrowser = true;
                        }
                    }
                }
            }
            else if (state.gameState == STATE_MENU) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    float mx = event.button.x;
                    float my = event.button.y;
                    float centerX = WINDOW_WIDTH / 2;
                    float centerY = WINDOW_HEIGHT / 2;

                    // Server address box
                    if (mx >= centerX - 200 && mx <= centerX + 200 && my >= centerY - 150 && my <= centerY - 100) {
                        state.editingServer = true;
                        state.editingName = false;
                        state.browser.editingCode = false;
                        state.inputBuffer = state.serverIP;
                        SDL_StartTextInput(state.window);
                    }
                    // Name box
                    else if (mx >= centerX - 200 && mx <= centerX + 200 && my >= centerY - 50 && my <= centerY) {
                        state.editingServer = false;
                        state.editingName = true;
                        state.browser.editingCode = false;
                        state.inputBuffer = state.playerName;
                        SDL_StartTextInput(state.window);
                    }
                    // Code box (NEW)
                    else if (mx >= centerX - 200 && mx <= centerX + 200 && my >= centerY + 50 && my <= centerY + 100) {
                        state.editingServer = false;
                        state.editingName = false;
                        state.browser.editingCode = true;
                        state.inputBuffer = state.browser.codeInput;
                        SDL_StartTextInput(state.window);
                    }
                    // Connect button
                    else if (!state.playerName.empty() && mx >= centerX - 100 && mx <= centerX + 100 &&
                        my >= centerY + 150 && my <= centerY + 210) {
                        state.gameState = STATE_CONNECTING;
                        state.errorMessage = "";
                        SDL_StopTextInput(state.window);
                    }
                    else {
                        if (state.editingServer) {
                            state.serverIP = state.inputBuffer;
                        }
                        if (state.editingName) {
                            state.playerName = state.inputBuffer;
                        }
                        if (state.browser.editingCode) {
                            state.browser.codeInput = state.inputBuffer;
                        }
                        state.editingServer = false;
                        state.editingName = false;
                        state.browser.editingCode = false;
                        SDL_StopTextInput(state.window);
                    }
                }

                if (event.type == SDL_EVENT_TEXT_INPUT) {
                    if (state.editingServer || state.editingName || state.browser.editingCode) {
                        state.inputBuffer += event.text.text;
                    }
                }

                if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.key == SDLK_BACKSPACE && (state.editingServer || state.editingName || state.browser.editingCode)) {
                        if (!state.inputBuffer.empty()) {
                            state.inputBuffer.pop_back();
                        }
                    }
                    else if (event.key.key == SDLK_RETURN) {
                        if (state.editingServer) {
                            state.serverIP = state.inputBuffer;
                            state.editingServer = false;
                            SDL_StopTextInput(state.window);
                        }
                        else if (state.editingName) {
                            state.playerName = state.inputBuffer;
                            state.editingName = false;
                            SDL_StopTextInput(state.window);
                        }
                        else if (state.browser.editingCode) {
                            state.browser.codeInput = state.inputBuffer;
                            state.browser.editingCode = false;
                            SDL_StopTextInput(state.window);
                        }
                    }
                    else if (event.key.key == SDLK_ESCAPE) {
                        state.editingServer = false;
                        state.editingName = false;
                        state.browser.editingCode = false;
                        SDL_StopTextInput(state.window);
                        state.gameState = STATE_BROWSER;
                        state.browser.state = BROWSER_MAIN_MENU;
                    }
                }
            }
            else if (state.gameState == STATE_PLAYING) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        sendSplit(&state);
                    }
                    else if (event.button.button == SDL_BUTTON_RIGHT) {
                        sendMerge(&state);
                    }
                }

                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    if (event.key.key == SDLK_W) state.keyW = true;
                    if (event.key.key == SDLK_A) state.keyA = true;
                    if (event.key.key == SDLK_S) state.keyS = true;
                    if (event.key.key == SDLK_D) state.keyD = true;

                    if (event.key.key == SDLK_ESCAPE) {
                        if (state.returnToBrowser) {
                            state.gameState = STATE_BROWSER;
                            state.browser.state = BROWSER_SERVER_LIST;
                        }
                        else {
                            state.gameState = STATE_MENU;
                        }
                        if (state.clientSocket != INVALID_SOCKET) {
                            closesocket(state.clientSocket);
                            state.clientSocket = INVALID_SOCKET;
                        }
                    }

                    if (event.key.key == SDLK_F11) {
                        bool isFullscreen = SDL_GetWindowFlags(state.window) & SDL_WINDOW_FULLSCREEN;
                        SDL_SetWindowFullscreen(state.window, !isFullscreen);
                    }
                }

                if (event.type == SDL_EVENT_KEY_UP) {
                    if (event.key.key == SDLK_W) state.keyW = false;
                    if (event.key.key == SDLK_A) state.keyA = false;
                    if (event.key.key == SDLK_S) state.keyS = false;
                    if (event.key.key == SDLK_D) state.keyD = false;
                }
            }
        }

        if (state.gameState == STATE_BROWSER) {
            drawServerBrowser(state.renderer, state.fontLarge, state.fontMedium,
                state.fontSmall, state.browser, WINDOW_WIDTH, WINDOW_HEIGHT);
            SDL_RenderPresent(state.renderer);
        }
        else if (state.gameState == STATE_MENU) {
            drawMenu(&state);
            SDL_RenderPresent(state.renderer);
        }
        else if (state.gameState == STATE_CONNECTING) {
            drawConnecting(&state);
            SDL_RenderPresent(state.renderer);

            bool connected = false;
            if (state.returnToBrowser && state.browser.selectedServer >= 0) {
                // Connecting from browser
                std::vector<ServerInfo> filteredServers;
                for (const auto& server : state.browser.servers) {
                    if (state.browser.searchQuery.empty()) {
                        filteredServers.push_back(server);
                    }
                    else {
                        std::string lowerServerName = server.name;
                        std::string lowerQuery = state.browser.searchQuery;

                        std::transform(lowerServerName.begin(), lowerServerName.end(), lowerServerName.begin(), ::tolower);
                        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

                        if (lowerServerName.find(lowerQuery) != std::string::npos) {
                            filteredServers.push_back(server);
                        }
                    }
                }

                if (state.browser.selectedServer < (int)filteredServers.size()) {
                    const ServerInfo& server = filteredServers[state.browser.selectedServer];
                    connected = connectToServerWithCode(&state, server.address, server.port,
                        server.hasPassword ? state.browser.codeInput : "");
                }
            }


            else {
                // Direct connect - use code if provided
                connected = connectToServerWithCode(&state, state.serverIP, 8888, state.browser.codeInput);
            }

            if (connected) {
                // Set player name from browser if connecting from browser
                if (state.returnToBrowser && !state.browser.nameInput.empty()) {
                    state.playerName = state.browser.nameInput;
                }
                // If name is STILL empty, use a default
                if (state.playerName.empty()) {
                    state.playerName = "Unnamed Player";
                }
                state.gameState = STATE_PLAYING;
            }
            else {
                if (state.returnToBrowser) {
                    state.gameState = STATE_BROWSER;
                    state.browser.state = BROWSER_SERVER_LIST;
                }
                else {
                    state.gameState = STATE_MENU;
                }
            }
        }
        else if (state.gameState == STATE_PLAYING) {
            processHeldKeys(&state);
            checkServerMessages(&state);

            SDL_SetRenderDrawColor(state.renderer, 50, 50, 50, 255);
            SDL_RenderClear(state.renderer);

            drawGrid(&state);
            drawFood(&state);

            for (const auto& pair : state.otherPlayers) {
                for (const auto& cell : pair.second.cells) {
                    drawCell(&state, cell, false);
                }
            }

            for (const auto& cell : state.myCells) {
                drawCell(&state, cell, true);
            }

            drawLeaderboard(&state);
            drawMinimap(&state);
            drawCellCount(&state);

            SDL_RenderPresent(state.renderer);
        }

        SDL_Delay(16);
    }

    if (state.clientSocket != INVALID_SOCKET) {
        closesocket(state.clientSocket);
    }

    if (state.fontLarge) TTF_CloseFont(state.fontLarge);
    if (state.fontMedium) TTF_CloseFont(state.fontMedium);
    if (state.fontSmall) TTF_CloseFont(state.fontSmall);
    SDL_DestroyRenderer(state.renderer);
    SDL_DestroyWindow(state.window);
    TTF_Quit();
    SDL_Quit();
    WSACleanup();

    return 0;
}