#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "network_common.h"

#pragma comment(lib, "ws2_32.lib")

// Server Finder configuration - change these to match your server finder
#ifndef SERVER_FINDER_IP_ADDR
#define SERVER_FINDER_IP_ADDR "::1"
#endif

#ifndef SERVER_FINDER_PORT_NUM
#define SERVER_FINDER_PORT_NUM 7777
#endif

enum BrowserState {
    BROWSER_MAIN_MENU,
    BROWSER_DIRECT_CONNECT,
    BROWSER_SERVER_LIST
};

struct BrowserContext {
    BrowserState state = BROWSER_MAIN_MENU;
    std::vector<ServerInfo> servers;
    int selectedServer = -1;
    int scrollOffset = 0;
    std::string searchQuery = "";
    bool sortBySize = false;  // false = largest first, true = smallest first
    std::string codeInput = "";
    std::string nameInput = "";  // NEW: Username input
    bool editingSearch = false;
    bool editingCode = false;
    bool editingName = false;  // NEW: Editing name flag
};

inline void queryServerFinder(std::vector<ServerInfo>& servers) {
    servers.clear();

    SOCKET socket = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) return;

    sockaddr_in6 finderAddr;
    memset(&finderAddr, 0, sizeof(finderAddr));
    finderAddr.sin6_family = AF_INET6;
    finderAddr.sin6_port = htons(SERVER_FINDER_PORT_NUM);
    inet_pton(AF_INET6, SERVER_FINDER_IP_ADDR, &finderAddr.sin6_addr);

    std::string queryMsg = "QUERY";
    sendto(socket, queryMsg.c_str(), queryMsg.length(), 0,
        (sockaddr*)&finderAddr, sizeof(finderAddr));

    // Wait for response with timeout
    u_long blockingMode = 0;
    ioctlsocket(socket, FIONBIO, &blockingMode);

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    int result = select(0, &readfds, NULL, NULL, &timeout);

    if (result > 0) {
        char buffer[8192];
        int recvLen = recvfrom(socket, buffer, sizeof(buffer) - 1, 0, NULL, NULL);

        if (recvLen > 0) {
            buffer[recvLen] = '\0';
            std::string response(buffer);

            if (response.substr(0, 8) == "SERVERS:") {
                std::string serverData = response.substr(8);
                std::stringstream ss(serverData);
                std::string serverToken;

                while (std::getline(ss, serverToken, ';')) {
                    if (serverToken.empty()) continue;

                    std::stringstream serverStream(serverToken);
                    std::string token;
                    std::vector<std::string> parts;

                    while (std::getline(serverStream, token, ',')) {
                        parts.push_back(token);
                    }

                    if (parts.size() >= 8) {
                        ServerInfo info;
                        info.name = parts[0];
                        info.address = parts[1];
                        info.port = std::stoi(parts[2]);
                        info.currentPlayers = std::stoi(parts[3]);
                        info.maxPlayers = std::stoi(parts[4]);
                        info.mapWidth = std::stoi(parts[5]);
                        info.mapHeight = std::stoi(parts[6]);
                        info.hasPassword = (parts[7] == "1");
                        info.serverCode = (parts.size() > 8) ? parts[8] : "";
                        servers.push_back(info);
                    }
                }
            }
        }
    }

    closesocket(socket);
}

inline void drawTextHelper(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
    float x, float y, SDL_Color color) {
    if (text.empty() || !font) return;

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.length(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect destRect = { x, y, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

inline void drawTextCenteredHelper(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
    float x, float y, SDL_Color color) {
    if (text.empty() || !font) return;

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), text.length(), color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
        SDL_DestroySurface(surface);
        return;
    }

    SDL_FRect destRect = { x - surface->w / 2.0f, y - surface->h / 2.0f,
                          (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, NULL, &destRect);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

inline void drawButtonHelper(SDL_Renderer* renderer, TTF_Font* font, const std::string& text,
    float x, float y, float w, float h) {
    SDL_SetRenderDrawColor(renderer, 70, 120, 200, 255);
    SDL_FRect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer, &rect);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &rect);

    SDL_Color white = { 255, 255, 255, 255 };
    drawTextCenteredHelper(renderer, font, text, x + w / 2, y + h / 2, white);
}

inline void drawServerBrowser(SDL_Renderer* renderer, TTF_Font* fontLarge, TTF_Font* fontMedium,
    TTF_Font* fontSmall, BrowserContext& browser,
    int windowWidth, int windowHeight) {
    SDL_SetRenderDrawColor(renderer, 30, 30, 50, 255);
    SDL_RenderClear(renderer);

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color gray = { 180, 180, 180, 255 };
    SDL_Color cyan = { 100, 255, 255, 255 };
    SDL_Color red = { 255, 100, 100, 255 };

    float centerX = windowWidth / 2.0f;

    if (browser.state == BROWSER_MAIN_MENU) {
        drawTextCenteredHelper(renderer, fontLarge, "Agar.io Clone", centerX, 100, white);

        float centerY = windowHeight / 2.0f;

        drawButtonHelper(renderer, fontMedium, "Browse Servers", centerX - 150, centerY - 80, 300, 60);
        drawButtonHelper(renderer, fontMedium, "Direct Connect", centerX - 150, centerY + 20, 300, 60);

        drawTextCenteredHelper(renderer, fontSmall, "LEFT CLICK: Split | RIGHT CLICK: Merge",
            centerX, windowHeight - 30, cyan);
    }
    else if (browser.state == BROWSER_SERVER_LIST) {
        drawTextCenteredHelper(renderer, fontLarge, "Server Browser", centerX, 50, white);

        // Search bar
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_FRect searchRect = { 50, 100, (float)windowWidth - 100, 40 };
        SDL_RenderFillRect(renderer, &searchRect);
        SDL_SetRenderDrawColor(renderer, browser.editingSearch ? 100 : 150, 150, 255, 255);
        SDL_RenderRect(renderer, &searchRect);

        std::string searchText = browser.searchQuery.empty() ? "Search servers..." : browser.searchQuery;
        if (browser.editingSearch && !browser.searchQuery.empty()) searchText += "_";
        else if (browser.editingSearch && browser.searchQuery.empty()) searchText = "_";

        drawTextHelper(renderer, fontSmall, searchText, 60, 110,
            browser.searchQuery.empty() && !browser.editingSearch ? gray : white);

        // Filter button
        drawButtonHelper(renderer, fontSmall,
            browser.sortBySize ? "Sort: Smallest" : "Sort: Largest",
            windowWidth - 200, 160, 150, 35);

        // Refresh button
        drawButtonHelper(renderer, fontSmall, "Refresh", windowWidth - 370, 160, 150, 35);

        // Filter servers by search (case-insensitive)
        std::vector<ServerInfo> filteredServers;
        for (const auto& server : browser.servers) {
            if (browser.searchQuery.empty()) {
                filteredServers.push_back(server);
            }
            else {
                // Convert both to lowercase for comparison
                std::string lowerServerName = server.name;
                std::string lowerQuery = browser.searchQuery;

                std::transform(lowerServerName.begin(), lowerServerName.end(), lowerServerName.begin(), ::tolower);
                std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

                if (lowerServerName.find(lowerQuery) != std::string::npos) {
                    filteredServers.push_back(server);
                }
            }
        }

        // Sort servers
        std::sort(filteredServers.begin(), filteredServers.end(),
            [&](const ServerInfo& a, const ServerInfo& b) {
                if (browser.sortBySize) {
                    return a.currentPlayers < b.currentPlayers;
                }
                else {
                    return a.currentPlayers > b.currentPlayers;
                }
            });

        // Draw server list
        float listY = 220;
        float listHeight = windowHeight - 220;
        int maxVisible = (int)(listHeight / 70);

        int startIdx = browser.scrollOffset;
        int endIdx = std::min(startIdx + maxVisible, (int)filteredServers.size());

        for (int i = startIdx; i < endIdx; i++) {
            const ServerInfo& server = filteredServers[i];
            float y = listY + (i - startIdx) * 70;

            // Server box
            bool isSelected = (i == browser.selectedServer);
            SDL_SetRenderDrawColor(renderer, isSelected ? 60 : 40, isSelected ? 60 : 40,
                isSelected ? 80 : 50, 255);
            SDL_FRect serverRect = { 50, y, (float)windowWidth - 100, 65 };
            SDL_RenderFillRect(renderer, &serverRect);
            SDL_SetRenderDrawColor(renderer, isSelected ? 100 : 80, isSelected ? 150 : 100,
                isSelected ? 255 : 150, 255);
            SDL_RenderRect(renderer, &serverRect);

            // Server name
            std::string displayName = server.name;
            if (server.hasPassword) displayName += " [CODE]";
            drawTextHelper(renderer, fontMedium, displayName, 60, y + 5, white);

            // Server info
            std::string info = std::to_string(server.currentPlayers) + "/" +
                std::to_string(server.maxPlayers) + " players | " +
                std::to_string(server.mapWidth) + "x" + std::to_string(server.mapHeight);
            drawTextHelper(renderer, fontSmall, info, 60, y + 35, gray);

            // Join button (only show if name is entered)
            if (isSelected && !browser.nameInput.empty()) {
                drawButtonHelper(renderer, fontSmall, "Join",
                    windowWidth - 180, y + 15, 100, 35);
            }
        }

        // Bottom input section - fixed position
        float bottomY = windowHeight - 150;

        // Username input
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_FRect nameRect = { centerX - 300, bottomY, 250, 40 };
        SDL_RenderFillRect(renderer, &nameRect);
        SDL_SetRenderDrawColor(renderer, browser.editingName ? 100 : 150, 150, 255, 255);
        SDL_RenderRect(renderer, &nameRect);

        drawTextHelper(renderer, fontSmall, "Username:", centerX - 300, bottomY - 25, gray);
        std::string nameText = browser.nameInput.empty() ? "Enter name..." : browser.nameInput;
        if (browser.editingName && !browser.nameInput.empty()) nameText += "_";
        else if (browser.editingName && browser.nameInput.empty()) nameText = "_";
        drawTextHelper(renderer, fontSmall, nameText, centerX - 290, bottomY + 10,
            browser.nameInput.empty() && !browser.editingName ? gray : white);

        // Code input
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_FRect codeRect = { centerX + 50, bottomY, 250, 40 };
        SDL_RenderFillRect(renderer, &codeRect);
        SDL_SetRenderDrawColor(renderer, browser.editingCode ? 100 : 150, 150, 255, 255);
        SDL_RenderRect(renderer, &codeRect);

        drawTextHelper(renderer, fontSmall, "Server Code (optional):", centerX + 50, bottomY - 25, gray);
        std::string codeText = browser.codeInput;
        if (browser.editingCode) codeText += "_";
        drawTextHelper(renderer, fontSmall, codeText, centerX + 60, bottomY + 10, white);

        // Show count
        std::string countText = std::to_string(filteredServers.size()) + " servers";
        drawTextHelper(renderer, fontSmall, countText, 50, 165, gray);
    }
}

inline void handleBrowserInput(BrowserContext& browser, SDL_Event& event,
    int windowWidth, int windowHeight) {
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
        float mx = event.button.x;
        float my = event.button.y;
        float centerX = windowWidth / 2.0f;
        float centerY = windowHeight / 2.0f;

        if (browser.state == BROWSER_MAIN_MENU) {
            // Browse Servers button (top)
            if (mx >= centerX - 150 && mx <= centerX + 150 &&
                my >= centerY - 80 && my <= centerY - 20) {
                browser.state = BROWSER_SERVER_LIST;
                queryServerFinder(browser.servers);
            }
            // Direct Connect button (bottom)
            else if (mx >= centerX - 150 && mx <= centerX + 150 &&
                my >= centerY + 20 && my <= centerY + 80) {
                browser.state = BROWSER_DIRECT_CONNECT;
            }
        }
        else if (browser.state == BROWSER_SERVER_LIST) {
            float bottomY = windowHeight - 150;

            bool clickedSearchBar = (mx >= 50 && mx <= windowWidth - 50 && my >= 100 && my <= 140);
            bool clickedNameBox = (mx >= centerX - 300 && mx <= centerX - 50 && my >= bottomY && my <= bottomY + 40);
            bool clickedCodeBox = (mx >= centerX + 50 && mx <= centerX + 300 && my >= bottomY && my <= bottomY + 40);
            bool clickedRefresh = (mx >= windowWidth - 370 && mx <= windowWidth - 220 && my >= 160 && my <= 195);
            bool clickedSort = (mx >= windowWidth - 200 && mx <= windowWidth - 50 && my >= 160 && my <= 195);

            // Search bar
            if (clickedSearchBar) {
                browser.editingSearch = true;
                browser.editingCode = false;
                browser.editingName = false;
            }
            // Username box
            else if (clickedNameBox) {
                browser.editingName = true;
                browser.editingSearch = false;
                browser.editingCode = false;
            }
            // Code input
            else if (clickedCodeBox) {
                browser.editingCode = true;
                browser.editingSearch = false;
                browser.editingName = false;
            }
            // Refresh button
            else if (clickedRefresh) {
                queryServerFinder(browser.servers);
                browser.selectedServer = -1;
            }
            // Sort button
            else if (clickedSort) {
                browser.sortBySize = !browser.sortBySize;
            }
            // Server list
            else if (mx >= 50 && mx <= windowWidth - 50 && my >= 220 && my < bottomY - 10) {
                int clickedIdx = browser.scrollOffset + (int)((my - 220) / 70);

                // Filter servers first (case-insensitive)
                std::vector<int> filteredIndices;
                for (int i = 0; i < (int)browser.servers.size(); i++) {
                    if (browser.searchQuery.empty()) {
                        filteredIndices.push_back(i);
                    }
                    else {
                        std::string lowerServerName = browser.servers[i].name;
                        std::string lowerQuery = browser.searchQuery;
                        std::transform(lowerServerName.begin(), lowerServerName.end(), lowerServerName.begin(), ::tolower);
                        std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);
                        if (lowerServerName.find(lowerQuery) != std::string::npos) {
                            filteredIndices.push_back(i);
                        }
                    }
                }

                if (clickedIdx >= 0 && clickedIdx < (int)filteredIndices.size()) {
                    browser.selectedServer = clickedIdx;

                    // Check if join button clicked (only if name is entered)
                    if (!browser.nameInput.empty()) {
                        float y = 220 + (clickedIdx - browser.scrollOffset) * 70;
                        if (mx >= windowWidth - 180 && mx <= windowWidth - 80 &&
                            my >= y + 15 && my <= y + 50) {
                            // Join clicked - handled by main game code
                        }
                    }
                }
            }
            else {
                // Clicked elsewhere - stop editing
                browser.editingSearch = false;
                browser.editingCode = false;
                browser.editingName = false;
            }
        }
    }
    else if (event.type == SDL_EVENT_TEXT_INPUT) {
        if (browser.editingSearch) {
            browser.searchQuery += event.text.text;
        }
        else if (browser.editingCode) {
            browser.codeInput += event.text.text;
        }
        else if (browser.editingName) {
            browser.nameInput += event.text.text;
        }
    }
    else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_BACKSPACE) {
            if (browser.editingSearch && !browser.searchQuery.empty()) {
                browser.searchQuery.pop_back();
            }
            else if (browser.editingCode && !browser.codeInput.empty()) {
                browser.codeInput.pop_back();
            }
            else if (browser.editingName && !browser.nameInput.empty()) {
                browser.nameInput.pop_back();
            }
        }
        else if (event.key.key == SDLK_RETURN) {
            browser.editingSearch = false;
            browser.editingCode = false;
            browser.editingName = false;
        }
        else if (event.key.key == SDLK_ESCAPE) {
            if (browser.state == BROWSER_SERVER_LIST) {
                browser.state = BROWSER_MAIN_MENU;
                browser.selectedServer = -1;
                browser.searchQuery = "";
                browser.codeInput = "";
                browser.nameInput = "";
            }
            browser.editingSearch = false;
            browser.editingCode = false;
            browser.editingName = false;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        if (browser.state == BROWSER_SERVER_LIST) {
            browser.scrollOffset -= event.wheel.y;
            if (browser.scrollOffset < 0) browser.scrollOffset = 0;

            int maxScroll = (int)browser.servers.size() - 5;
            if (maxScroll < 0) maxScroll = 0;
            if (browser.scrollOffset > maxScroll) browser.scrollOffset = maxScroll;
        }
    }
}