#include "map_state_http.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "map_state_capture.h"
#include "map_state_json.h"
#include "plugin_config.h"
#include "plugin_helpers.h"

#include <atomic>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

namespace MapStateRuntime
{
namespace Detail
{
	namespace
	{
		constexpr int kHttpReadBufferSize = 8192;
		constexpr int kHttpSelectTimeoutMs = 250;
		constexpr int kDefaultHttpPort = 9000;
		constexpr const char* kLoopbackAddress = "127.0.0.1";

		std::thread g_httpThread;
		std::atomic<bool> g_httpStopRequested = false;
		SOCKET g_httpListenSocket = INVALID_SOCKET;
		bool g_wsaStarted = false;
		int g_httpPort = kDefaultHttpPort;

		bool SendAll(SOCKET socketHandle, const std::string& data)
		{
			const char* buffer = data.data();
			int remaining = static_cast<int>(data.size());
			while (remaining > 0)
			{
				const int sent = send(socketHandle, buffer, remaining, 0);
				if (sent == SOCKET_ERROR)
				{
					return false;
				}
				buffer += sent;
				remaining -= sent;
			}
			return true;
		}

		void SendHttpResponse(SOCKET socketHandle, int statusCode, const char* statusText, const char* contentType, const std::string& body)
		{
			std::ostringstream header;
			header << "HTTP/1.1 " << statusCode << ' ' << statusText << "\r\n"
				<< "Connection: close\r\n"
				<< "Content-Type: " << contentType << "\r\n"
				<< "Content-Length: " << body.size() << "\r\n"
				<< "Cache-Control: no-store\r\n"
				<< "Access-Control-Allow-Origin: *\r\n"
				<< "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
				<< "Access-Control-Allow-Headers: Content-Type\r\n"
				<< "\r\n";
			std::string payload = header.str();
			payload += body;
			SendAll(socketHandle, payload);
		}

		void SendJsonResponse(SOCKET socketHandle, int statusCode, const char* statusText, const std::string& json)
		{
			SendHttpResponse(socketHandle, statusCode, statusText, "application/json; charset=utf-8", json);
		}

		void HandleHttpClient(SOCKET clientSocket)
		{
			char buffer[kHttpReadBufferSize + 1]{};
			const int received = recv(clientSocket, buffer, kHttpReadBufferSize, 0);
			if (received <= 0)
			{
				return;
			}

			buffer[received] = '\0';
			const std::string requestText(buffer, received);
			std::istringstream request(requestText);
			std::string method;
			std::string path;
			std::string version;
			request >> method >> path >> version;
			if (method.empty() || path.empty())
			{
				SendJsonResponse(clientSocket, 400, "Bad Request", "{\"ok\":false,\"error\":\"invalid_request\"}");
				return;
			}

			const size_t queryPos = path.find('?');
			if (queryPos != std::string::npos)
			{
				path = path.substr(0, queryPos);
			}

			if (method == "OPTIONS")
			{
				SendHttpResponse(clientSocket, 204, "No Content", "text/plain; charset=utf-8", {});
				return;
			}

			if (method != "GET")
			{
				SendJsonResponse(clientSocket, 405, "Method Not Allowed", "{\"ok\":false,\"error\":\"method_not_allowed\"}");
				return;
			}

			const CargoSnapshot snapshot = CopySnapshot();
			if (path == "/health")
			{
				SendJsonResponse(clientSocket, 200, "OK", BuildHealthJson(snapshot, g_httpPort));
				return;
			}
			if (path == "/cargo")
			{
				SendJsonResponse(clientSocket, 200, "OK", BuildCargoJson(snapshot));
				return;
			}
			if (path == "/rupture-cycle")
			{
				SendJsonResponse(clientSocket, 200, "OK", BuildRuptureCycleJson(snapshot));
				return;
			}
			if (path == "/" || path == "/index.html")
			{
				SendJsonResponse(clientSocket, 200, "OK", "{\"ok\":true,\"endpoints\":[\"/health\",\"/cargo\",\"/rupture-cycle\"]}");
				return;
			}

			SendJsonResponse(clientSocket, 404, "Not Found", "{\"ok\":false,\"error\":\"not_found\"}");
		}

		void HttpServerMain()
		{
			while (!g_httpStopRequested.load())
			{
				fd_set readSet;
				FD_ZERO(&readSet);
				FD_SET(g_httpListenSocket, &readSet);

				timeval timeout{};
				timeout.tv_sec = 0;
				timeout.tv_usec = kHttpSelectTimeoutMs * 1000;

				const int result = select(0, &readSet, nullptr, nullptr, &timeout);
				if (g_httpStopRequested.load())
				{
					break;
				}
				if (result == SOCKET_ERROR)
				{
					const int error = WSAGetLastError();
					if (!g_httpStopRequested.load())
					{
						LOG_ERROR("HTTP select() failed on port %d: %d", g_httpPort, error);
					}
					break;
				}
				if (result == 0 || !FD_ISSET(g_httpListenSocket, &readSet))
				{
					continue;
				}

				SOCKET clientSocket = accept(g_httpListenSocket, nullptr, nullptr);
				if (clientSocket == INVALID_SOCKET)
				{
					const int error = WSAGetLastError();
					if (!g_httpStopRequested.load())
					{
						LOG_WARN("HTTP accept() failed on port %d: %d", g_httpPort, error);
					}
					continue;
				}

				HandleHttpClient(clientSocket);
				closesocket(clientSocket);
			}
		}
	}

	bool StartHttpServer()
	{
		if (g_httpListenSocket != INVALID_SOCKET)
		{
			return true;
		}

		g_httpPort = MapExtensionPluginConfig::Config::HttpPort();
		if (g_httpPort <= 0)
		{
			g_httpPort = kDefaultHttpPort;
		}

		WSADATA wsaData{};
		const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (wsaResult != 0)
		{
			LOG_ERROR("HTTP WSAStartup failed: %d", wsaResult);
			return false;
		}
		g_wsaStarted = true;

		g_httpListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (g_httpListenSocket == INVALID_SOCKET)
		{
			LOG_ERROR("HTTP socket() failed: %d", WSAGetLastError());
			WSACleanup();
			g_wsaStarted = false;
			return false;
		}

		const char reuse = 1;
		setsockopt(g_httpListenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(static_cast<u_short>(g_httpPort));
		inet_pton(AF_INET, kLoopbackAddress, &addr.sin_addr);

		if (bind(g_httpListenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
		{
			LOG_ERROR("HTTP bind() failed on %s:%d: %d", kLoopbackAddress, g_httpPort, WSAGetLastError());
			closesocket(g_httpListenSocket);
			g_httpListenSocket = INVALID_SOCKET;
			WSACleanup();
			g_wsaStarted = false;
			return false;
		}

		if (listen(g_httpListenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			LOG_ERROR("HTTP listen() failed on port %d: %d", g_httpPort, WSAGetLastError());
			closesocket(g_httpListenSocket);
			g_httpListenSocket = INVALID_SOCKET;
			WSACleanup();
			g_wsaStarted = false;
			return false;
		}

		g_httpStopRequested = false;
		g_httpThread = std::thread(HttpServerMain);
		LOG_INFO("HTTP cargo endpoint listening on http://%s:%d", kLoopbackAddress, g_httpPort);
		return true;
	}

	void StopHttpServer()
	{
		g_httpStopRequested = true;
		if (g_httpListenSocket != INVALID_SOCKET)
		{
			closesocket(g_httpListenSocket);
			g_httpListenSocket = INVALID_SOCKET;
		}
		if (g_httpThread.joinable())
		{
			g_httpThread.join();
		}
		if (g_wsaStarted)
		{
			WSACleanup();
			g_wsaStarted = false;
		}
	}

	void HandleHttpProcessDetach(bool processTerminating)
	{
		if (!processTerminating)
		{
			return;
		}

		// During process teardown the CRT can destroy joinable std::thread objects
		// after plugin shutdown opportunities are gone, which triggers std::terminate.
		// Detach the listener thread here so process exit can continue cleanly.
		g_httpStopRequested = true;
		if (g_httpListenSocket != INVALID_SOCKET)
		{
			closesocket(g_httpListenSocket);
			g_httpListenSocket = INVALID_SOCKET;
		}
		if (g_httpThread.joinable())
		{
			g_httpThread.detach();
		}
	}
}
}
