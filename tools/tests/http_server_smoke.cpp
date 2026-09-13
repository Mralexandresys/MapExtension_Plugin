// Standalone Winsock regression test: uses the real HTTP server with game capture
// and serialization stubbed out. See DEVELOPERS.md for the build command.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "map_state_http.h"
#include "map_state_capture.h"
#include "map_state_json.h"
#include "plugin_config.h"
#include "plugin_helpers.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

IPluginLogger* GetLogger() { return nullptr; }
const IPluginSelf* GetPluginSelf() { return nullptr; }

namespace MapStateRuntime::Detail
{
    CargoSnapshot CopySnapshot() { return {}; }
    void RequestCargoSnapshotRefresh(const char*) {}
    std::string BuildHealthJson(const CargoSnapshot&, int) { return "{\"ok\":true}"; }
    std::string BuildCargoJson(const CargoSnapshot&) { return std::string(16 * 1024 * 1024, ' '); }
    std::string BuildRuptureCycleJson(const CargoSnapshot&) { return "{}"; }
}

namespace
{
    using Clock = std::chrono::steady_clock;
    using namespace MapStateRuntime::Detail;
    int port = 0;

    void Require(bool success, const char* message)
    {
        if (!success) throw std::runtime_error(message);
    }

    struct Socket
    {
        SOCKET value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Socket() { Require(value != INVALID_SOCKET, "socket failed"); }
        ~Socket() { closesocket(value); }
        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;

        void Connect()
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<u_short>(port));
            inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
            Require(connect(value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "connect failed");
        }
    };

    long long ElapsedMs(Clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    }

    void Start() { Require(StartHttpServer(), "HTTP start failed"); }
    void CheckStop(const char* label)
    {
        const auto start = Clock::now();
        StopHttpServer();
        const auto elapsed = ElapsedMs(start);
        std::cout << label << ": " << elapsed << " ms\n";
        Require(elapsed < 2000, "shutdown exceeded bounded I/O timeout");
    }
}

int main()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
    try
    {
        // Select an unused loopback port without touching the game's default API.
        {
            Socket probe;
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            Require(bind(probe.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind probe failed");
            int length = sizeof(address);
            Require(getsockname(probe.value, reinterpret_cast<sockaddr*>(&address), &length) == 0, "getsockname failed");
            port = ntohs(address.sin_port);
        }
        IPluginConfig config{};
        config.ReadInt = [](const IPluginSelf*, const char*, const char*, int) { return port; };
        config.InitializeFromSchema = [](const IPluginSelf*, const ConfigSchema*) { return true; };
        IPluginSelf self{};
        self.config = &config;
        MapExtensionPluginConfig::Config::Initialize(&self);

        Start();
        {
            Socket idle;
            idle.Connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            Socket request;
            request.Connect();
            const DWORD timeoutMs = 3000;
            setsockopt(request.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            const char* get = "GET /health HTTP/1.1\r\nHost: localhost\r\n\r\n";
            Require(send(request.value, get, static_cast<int>(std::strlen(get)), 0) > 0, "GET send failed");
            const auto start = Clock::now();
            std::string response;
            char buffer[1024];
            int received;
            while ((received = recv(request.value, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, received);
            Require(received == 0 && response.find("200 OK") != std::string::npos && response.find("{\"ok\":true}") != std::string::npos,
                "idle client blocked the following health request");
            std::cout << "health behind idle client: " << ElapsedMs(start) << " ms\n";
        }
        {
            Socket idle;
            idle.Connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CheckStop("shutdown with silent client");
        }
        Start();
        {
            Socket slowReader;
            const int receiveBufferSize = 1024;
            setsockopt(slowReader.value, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&receiveBufferSize), sizeof(receiveBufferSize));
            slowReader.Connect();
            const char* get = "GET /cargo HTTP/1.1\r\nHost: localhost\r\n\r\n";
            Require(send(slowReader.value, get, static_cast<int>(std::strlen(get)), 0) > 0, "cargo GET send failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CheckStop("shutdown with non-reading client");
        }
        Start();
        CheckStop("shutdown after restart");
        MapExtensionPluginConfig::Config::Initialize(nullptr);
        WSACleanup();
        std::cout << "HTTP smoke checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        StopHttpServer();
        WSACleanup();
        return 1;
    }
}
