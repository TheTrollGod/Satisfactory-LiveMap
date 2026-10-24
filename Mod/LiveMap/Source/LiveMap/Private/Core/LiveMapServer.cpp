#include "LiveMapServer.h"
#include "LiveMapCore.h"

#include "civetweb.h"

#include <algorithm>
#include <cstring>

namespace livemap
{
	Server::~Server()
	{
		Stop();
	}

	bool Server::Start(const ServerConfig& InConfig, std::string* OutError)
	{
		if (Context)
		{
			return true;
		}
		Config = InConfig;

		// Safe to call repeatedly; civetweb refcounts it.
		mg_init_library(0);

		const std::string Port = std::to_string(Config.Port);
		const std::string Threads = std::to_string(Config.WorkerThreads);
		std::vector<const char*> Options = {
			"listening_ports", Port.c_str(),
			"num_threads", Threads.c_str(),
			"request_timeout_ms", "5000",
			"websocket_timeout_ms", "3600000",
			"enable_keep_alive", "no",
		};
		if (!Config.DocumentRoot.empty())
		{
			Options.push_back("document_root");
			Options.push_back(Config.DocumentRoot.c_str());
		}
		Options.push_back(nullptr);

		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			bRunning = true;
			Queue.clear();
			bResyncAll = false;
			bNewClients = false;
			if (!LatestSnapshot)
			{
				LatestSnapshot = std::make_shared<const std::string>(FrameDiffer().Snapshot());
			}
		}
		Sender = std::thread(&Server::SenderLoop, this);

		mg_callbacks Callbacks;
		std::memset(&Callbacks, 0, sizeof(Callbacks));
		mg_init_data Init;
		std::memset(&Init, 0, sizeof(Init));
		Init.callbacks = &Callbacks;
		Init.user_data = this;
		Init.configuration_options = Options.data();

		char ErrBuf[256] = {0};
		mg_error_data Err;
		std::memset(&Err, 0, sizeof(Err));
		Err.text = ErrBuf;
		Err.text_buffer_size = sizeof(ErrBuf);

		Context = mg_start2(&Init, &Err);
		if (!Context)
		{
			if (OutError)
			{
				*OutError = ErrBuf[0] ? ErrBuf : "mg_start2 failed";
			}
			{
				std::lock_guard<std::mutex> Lock(QueueMutex);
				bRunning = false;
			}
			QueueCv.notify_all();
			Sender.join();
			mg_exit_library();
			return false;
		}

		mg_set_request_handler(Context, "/health$", &Server::OnHealth, this);
		mg_set_request_handler(Context, "/static/", &Server::OnStatic, this);
		mg_set_websocket_handler(Context, "/ws$", &Server::OnWsConnect, &Server::OnWsReady,
			&Server::OnWsData, &Server::OnWsClose, this);
		return true;
	}

	void Server::Stop()
	{
		if (!Context)
		{
			return;
		}
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			bRunning = false;
		}
		QueueCv.notify_all();
		if (Sender.joinable())
		{
			Sender.join();
		}
		// Joins civetweb workers; close handlers run before this returns.
		mg_stop(Context);
		Context = nullptr;
		mg_exit_library();

		std::lock_guard<std::mutex> Lock(ClientsMutex);
		Clients.clear();
	}

	void Server::Publish(std::string Snapshot, std::string DeltaBody, uint64_t Seq)
	{
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			LatestSnapshot = std::make_shared<const std::string>(std::move(Snapshot));
			LatestSeq = Seq;
			if (!DeltaBody.empty())
			{
				if (Queue.size() >= Config.MaxQueuedDeltas)
				{
					// Sender is behind (slow client). Deltas are incremental, so we
					// can't drop just some: drop all and resend the full state.
					Queue.clear();
					bResyncAll = true;
					++Resyncs;
				}
				else
				{
					Queue.push_back({Seq, std::make_shared<const std::string>(std::move(DeltaBody))});
				}
			}
		}
		QueueCv.notify_one();
	}

	void Server::RequestResync()
	{
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			bResyncAll = true;
		}
		QueueCv.notify_one();
	}

	uint64_t Server::SetStatic(const std::string& Name, std::string Body)
	{
		std::lock_guard<std::mutex> Lock(StaticMutex);
		StaticDoc& Doc = Statics[Name];
		Doc.Version = ++StaticCounter;
		Doc.Body = std::make_shared<const std::string>(std::move(Body));
		return Doc.Version;
	}

	size_t Server::ClientCount() const
	{
		std::lock_guard<std::mutex> Lock(ClientsMutex);
		return Clients.size();
	}

	uint64_t Server::ResyncCount() const
	{
		std::lock_guard<std::mutex> Lock(QueueMutex);
		return Resyncs;
	}

	bool Server::Send(mg_connection* Conn, const std::string& Body)
	{
		return mg_websocket_write(Conn, MG_WEBSOCKET_OPCODE_TEXT, Body.data(), Body.size()) > 0;
	}

	void Server::SenderLoop()
	{
		for (;;)
		{
			std::deque<Delta> Batch;
			std::shared_ptr<const std::string> Snapshot;
			uint64_t SnapshotSeq = 0;
			bool bResync = false;
			{
				std::unique_lock<std::mutex> Lock(QueueMutex);
				QueueCv.wait(Lock, [this] { return !bRunning || !Queue.empty() || bResyncAll || bNewClients; });
				if (!bRunning)
				{
					return;
				}
				Batch.swap(Queue);
				Snapshot = LatestSnapshot;
				SnapshotSeq = LatestSeq;
				bResync = bResyncAll;
				bResyncAll = false;
				bNewClients = false;
			}

			const std::string Hello = [this] {
				JsonWriter W;
				W.BeginObject();
				W.Key("type"); W.String("hello");
				W.Key("version"); W.Int(1);
				W.Key("tickHz"); W.Int(Config.TickHz);
				W.EndObject();
				return W.Take();
			}();

			std::lock_guard<std::mutex> Lock(ClientsMutex);
			for (auto& C : Clients)
			{
				if (!C->bReady)
				{
					Send(C->Conn, Hello);
					Send(C->Conn, *Snapshot);
					C->Seq = SnapshotSeq;
					C->bReady = true;
				}
				else if (bResync)
				{
					Send(C->Conn, *Snapshot);
					C->Seq = SnapshotSeq;
				}
			}
			for (const Delta& D : Batch)
			{
				for (auto& C : Clients)
				{
					// A client whose snapshot already includes this delta skips it.
					if (C->bReady && D.Seq > C->Seq)
					{
						Send(C->Conn, *D.Body);
						C->Seq = D.Seq;
					}
				}
			}
		}
	}

	int Server::OnHealth(mg_connection* Conn, void* User)
	{
		Server* Self = static_cast<Server*>(User);
		uint64_t Seq;
		{
			std::lock_guard<std::mutex> Lock(Self->QueueMutex);
			Seq = Self->LatestSeq;
		}
		JsonWriter W;
		W.BeginObject();
		W.Key("ok"); W.Raw("true");
		W.Key("clients"); W.Int(static_cast<int64_t>(Self->ClientCount()));
		W.Key("seq"); W.Int(static_cast<int64_t>(Seq));
		W.EndObject();
		const std::string& Body = W.Str();
		mg_printf(Conn,
			"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n"
			"Content-Length: %u\r\nConnection: close\r\n\r\n",
			static_cast<unsigned>(Body.size()));
		mg_write(Conn, Body.data(), Body.size());
		return 200;
	}

	int Server::OnStatic(mg_connection* Conn, void* User)
	{
		Server* Self = static_cast<Server*>(User);
		const mg_request_info* Info = mg_get_request_info(Conn);
		static const char Prefix[] = "/static/";
		const char* Uri = Info && Info->local_uri ? Info->local_uri : "";
		const std::string Name = std::strncmp(Uri, Prefix, sizeof(Prefix) - 1) == 0 ? Uri + sizeof(Prefix) - 1 : "";

		StaticDoc Doc;
		{
			std::lock_guard<std::mutex> Lock(Self->StaticMutex);
			auto It = Self->Statics.find(Name);
			if (It != Self->Statics.end())
			{
				Doc = It->second;
			}
		}
		if (!Doc.Body)
		{
			mg_printf(Conn,
				"HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n"
				"Content-Length: 9\r\nConnection: close\r\n\r\nnot found");
			return 404;
		}
		// The body is a shared_ptr copy, so a concurrent SetStatic can't free it mid-write.
		mg_printf(Conn,
			"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n"
			"Access-Control-Expose-Headers: X-LiveMap-Version\r\nCache-Control: no-store\r\n"
			"X-LiveMap-Version: %llu\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
			static_cast<unsigned long long>(Doc.Version), static_cast<unsigned long long>(Doc.Body->size()));
		mg_write(Conn, Doc.Body->data(), Doc.Body->size());
		return 200;
	}

	int Server::OnWsConnect(const mg_connection*, void*)
	{
		return 0; // accept. TODO(M5): check auth token here.
	}

	void Server::OnWsReady(mg_connection* Conn, void* User)
	{
		Server* Self = static_cast<Server*>(User);
		{
			std::lock_guard<std::mutex> Lock(Self->ClientsMutex);
			auto C = std::make_unique<Client>();
			C->Conn = Conn;
			Self->Clients.push_back(std::move(C));
		}
		{
			std::lock_guard<std::mutex> Lock(Self->QueueMutex);
			Self->bNewClients = true;
		}
		Self->QueueCv.notify_one();
	}

	int Server::OnWsData(mg_connection*, int Bits, char*, size_t, void*)
	{
		// Client messages (subscribe) are ignored for now. Close on a close frame.
		return (Bits & 0x0F) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE ? 0 : 1;
	}

	void Server::OnWsClose(const mg_connection* Conn, void* User)
	{
		Server* Self = static_cast<Server*>(User);
		std::lock_guard<std::mutex> Lock(Self->ClientsMutex);
		auto& V = Self->Clients;
		V.erase(std::remove_if(V.begin(), V.end(), [Conn](const std::unique_ptr<Client>& C) { return C->Conn == Conn; }), V.end());
	}
}
