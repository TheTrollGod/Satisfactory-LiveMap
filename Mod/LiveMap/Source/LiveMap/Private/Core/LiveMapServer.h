#pragma once

// Engine-free WebSocket server on top of civetweb.
//
// Threads:
//   game thread    -> Publish() only. Never blocks on the network.
//   sender thread  -> owns every websocket write, in seq order.
//   civetweb pool  -> accepts connections, calls the handlers below.

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct mg_context;
struct mg_connection;

namespace livemap
{
	struct ServerConfig
	{
		int Port = 8086;
		/** civetweb holds one worker per open websocket, so this caps viewers. */
		int WorkerThreads = 16;
		/** If more deltas than this are waiting, drop them and resync everyone with a snapshot. */
		size_t MaxQueuedDeltas = 64;
		int TickHz = 5;
		/** Optional directory to serve the built frontend from. Empty disables static files. */
		std::string DocumentRoot;
	};

	class Server
	{
	public:
		Server() = default;
		~Server();
		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		bool Start(const ServerConfig& Config, std::string* OutError);
		void Stop();
		bool IsRunning() const { return Context != nullptr; }

		/**
		 * Game thread. Snapshot is the full state after applying Delta (which may be
		 * empty). Seq must be the differ's Seq() after that delta.
		 */
		void Publish(std::string Snapshot, std::string Delta, uint64_t Seq);

		/** Game thread. Resend the snapshot to every client, e.g. after the static layer changed. */
		void RequestResync();

		size_t ClientCount() const;
		uint64_t ResyncCount() const;

	private:
		struct Client
		{
			mg_connection* Conn = nullptr;
			bool bReady = false;
			uint64_t Seq = 0;
		};
		struct Delta
		{
			uint64_t Seq;
			std::shared_ptr<const std::string> Body;
		};

		static int OnHealth(mg_connection* Conn, void* User);
		static int OnWsConnect(const mg_connection* Conn, void* User);
		static void OnWsReady(mg_connection* Conn, void* User);
		static int OnWsData(mg_connection* Conn, int Bits, char* Data, size_t Len, void* User);
		static void OnWsClose(const mg_connection* Conn, void* User);

		void SenderLoop();
		static bool Send(mg_connection* Conn, const std::string& Body);

		ServerConfig Config;
		mg_context* Context = nullptr;

		// Game thread <-> sender thread.
		mutable std::mutex QueueMutex;
		std::condition_variable QueueCv;
		std::deque<Delta> Queue;
		std::shared_ptr<const std::string> LatestSnapshot;
		uint64_t LatestSeq = 0;
		bool bResyncAll = false;
		bool bNewClients = false;
		bool bRunning = false;
		uint64_t Resyncs = 0;

		// Sender thread <-> civetweb threads.
		mutable std::mutex ClientsMutex;
		std::vector<std::unique_ptr<Client>> Clients;

		std::thread Sender;
	};
}
