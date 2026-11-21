#pragma once

#include "Pch.h"
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>

namespace SkyPotion::Server {

    class WebSocketServer {
    public:
        static WebSocketServer& GetSingleton() {
            static WebSocketServer instance;
            return instance;
        }

        bool Start(uint16_t port = 47778, const std::string& host = "0.0.0.0");
        void Stop();
        bool IsRunning() const;
        bool HasConnectedClients() const;

        void Broadcast(const nlohmann::json& message);
        void BroadcastFullSnapshot();
        void BroadcastGameData();
        void BroadcastGameSessionLoaded();

        void BroadcastBinary(const std::string& binaryPayload);
        void BroadcastProtoSnapshot(); // 초기 접속 / 세이브 로드 시 CatalogSnapshot + BenchState 전송

    private:
        WebSocketServer();
        ~WebSocketServer();

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;

        void OnMessage(std::shared_ptr<ix::ConnectionState> connectionState,
                       ix::WebSocket& webSocket,
                       const ix::WebSocketMessagePtr& msg);

        static nlohmann::json ProcessQuery(const std::string& id, const nlohmann::json& fields);
        static nlohmann::json ProcessCommand(const std::string& action, const nlohmann::json& payload);

        std::unique_ptr<ix::WebSocketServer> _server;
        std::atomic<bool> _running{false};
        uint16_t _port{47778};
        std::string _host{"0.0.0.0"};
        std::atomic<int> _clientCount{ 0 };

        void HandleProtoCommand(ix::WebSocket& webSocket, const std::string& rawBytes);
    };

}
