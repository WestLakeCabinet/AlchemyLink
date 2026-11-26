#pragma once

#include "Pch.h"

namespace SkyPotion::Server {

    class Session;

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

        void BroadcastBinary(const std::string& binaryPayload);
        void BroadcastProtoSnapshot(); // 초기 접속 / 세이브 로드 시 CatalogSnapshot + BenchState 전송
        void ScheduleSessionSnapshot(int attempt = 0); // 세이브 로드 / 접속 시 준비 대기 및 자동 재시도 스냅샷
        void CancelScheduledSnapshot();

    private:
        friend class Session;

        struct Impl;

        WebSocketServer();
        ~WebSocketServer();

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;

        void ScheduleSessionSnapshotWithGen(int attempt, uint64_t gen);

        void RegisterSession(const std::shared_ptr<Session>& session);
        void UnregisterSession(const Session* session);
        void HandleSessionMessage(const std::shared_ptr<Session>& session, bool isBinary, std::string payload);
        void HandleProtoCommand(const std::shared_ptr<Session>& session, const std::string& rawBytes);

        std::unique_ptr<Impl> _impl;
        std::atomic<bool> _running{false};
        uint16_t _port{47778};
        std::string _host{"0.0.0.0"};
        std::atomic<int> _clientCount{0};
        std::atomic<uint64_t> _snapshotGeneration{0};
    };

}
