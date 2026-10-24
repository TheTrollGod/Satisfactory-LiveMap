// Integration tests: real civetweb server, real websocket clients, a fake
// "game thread" publishing frames. Meant to run under -fsanitize=thread too.
#include "LiveMapCore.h"
#include "LiveMapServer.h"
#include "check.h"

#include "civetweb.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace livemap;
using namespace std::chrono_literals;

static int GPort = 18086;

struct TestClient
{
	std::mutex Mutex;
	std::vector<std::string> Messages;
	std::atomic<bool> Closed{false};
	mg_connection* Conn = nullptr;

	static int OnData(mg_connection*, int Bits, char* Data, size_t Len, void* User)
	{
		auto* Self = static_cast<TestClient*>(User);
		if ((Bits & 0x0F) == MG_WEBSOCKET_OPCODE_TEXT)
		{
			std::lock_guard<std::mutex> Lock(Self->Mutex);
			Self->Messages.emplace_back(Data, Len);
		}
		return 1;
	}
	static void OnClose(const mg_connection*, void* User)
	{
		static_cast<TestClient*>(User)->Closed = true;
	}

	bool Connect(int Port)
	{
		char Err[256] = {0};
		Conn = mg_connect_websocket_client("127.0.0.1", Port, 0, Err, sizeof(Err), "/ws", nullptr, &OnData, &OnClose, this);
		if (!Conn)
		{
			std::fprintf(stderr, "connect failed: %s\n", Err);
		}
		return Conn != nullptr;
	}
	void Close()
	{
		if (Conn)
		{
			mg_websocket_client_write(Conn, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, "", 0);
			mg_close_connection(Conn);
			Conn = nullptr;
		}
	}
	std::vector<std::string> Snapshot()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Messages;
	}
	bool WaitFor(size_t N, std::chrono::milliseconds Timeout = 3000ms)
	{
		const auto End = std::chrono::steady_clock::now() + Timeout;
		while (std::chrono::steady_clock::now() < End)
		{
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Messages.size() >= N)
				{
					return true;
				}
			}
			std::this_thread::sleep_for(5ms);
		}
		return false;
	}
	bool WaitForSeq(uint64_t Seq, std::chrono::milliseconds Timeout = 5000ms)
	{
		const std::string Needle = "\"seq\":" + std::to_string(Seq) + ",";
		const auto End = std::chrono::steady_clock::now() + Timeout;
		while (std::chrono::steady_clock::now() < End)
		{
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (!Messages.empty() && CONTAINS(Messages.back(), Needle))
				{
					return true;
				}
			}
			std::this_thread::sleep_for(5ms);
		}
		return false;
	}
	~TestClient() { Close(); }
};

static uint64_t SeqOf(const std::string& M)
{
	const size_t P = M.find("\"seq\":");
	return P == std::string::npos ? 0 : std::strtoull(M.c_str() + P + 6, nullptr, 10);
}

static bool IsType(const std::string& M, const char* T)
{
	return CONTAINS(M, std::string("{\"type\":\"") + T + "\"");
}

// Every client stream must be: hello, snapshot, then strictly increasing seqs,
// where a snapshot may appear again (resync) but never goes backwards.
static void CheckStream(const std::vector<std::string>& M, uint64_t ExpectFinal)
{
	CHECK(M.size() >= 2);
	if (M.size() < 2)
	{
		return;
	}
	CHECK(IsType(M[0], "hello"));
	CHECK(IsType(M[1], "snapshot"));
	uint64_t Last = SeqOf(M[1]);
	for (size_t I = 2; I < M.size(); ++I)
	{
		const uint64_t S = SeqOf(M[I]);
		if (IsType(M[I], "snapshot"))
		{
			CHECK(S >= Last);
		}
		else
		{
			CHECK(IsType(M[I], "delta"));
			if (S != Last + 1 && S <= Last)
			{
				std::fprintf(stderr, "out of order: %llu after %llu\n", (unsigned long long)S, (unsigned long long)Last);
			}
			CHECK(S > Last);
		}
		Last = S;
	}
	CHECK_EQ(Last, ExpectFinal);
}

struct Game
{
	FrameDiffer Differ;
	Frame F;

	void Tick(Server& S, int I)
	{
		F[ELayer::Players].clear();
		Entity E;
		E.Id = "p1";
		E.X = I * 100.0;
		F[ELayer::Players].push_back(E);
		std::string Delta = Differ.Update(F);
		S.Publish(Differ.Snapshot(), std::move(Delta), Differ.Seq());
	}
};

static void TestHandshakeAndDeltas()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	std::string Err;
	CHECK(S.Start(C, &Err));

	Game G;
	G.Tick(S, 1);

	TestClient A;
	CHECK(A.Connect(GPort));
	CHECK(A.WaitFor(2));
	auto M = A.Snapshot();
	CHECK(IsType(M[0], "hello"));
	CHECK(IsType(M[1], "snapshot"));
	CHECK_EQ(SeqOf(M[1]), 1u);
	CHECK(CONTAINS(M[1], "\"pos\":[100,0,0]"));

	for (int I = 2; I <= 10; ++I)
	{
		G.Tick(S, I);
	}
	CHECK(A.WaitForSeq(10));
	CheckStream(A.Snapshot(), 10);
	CHECK_EQ(S.ClientCount(), 1u);
	S.Stop();
}

static void TestLateJoinerGetsNoStaleDeltas()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	CHECK(S.Start(C, nullptr));
	Game G;
	for (int I = 1; I <= 50; ++I)
	{
		G.Tick(S, I);
	}
	TestClient B;
	CHECK(B.Connect(GPort));
	CHECK(B.WaitFor(2));
	for (int I = 51; I <= 60; ++I)
	{
		G.Tick(S, I);
	}
	CHECK(B.WaitForSeq(60));
	auto M = B.Snapshot();
	CheckStream(M, 60);
	CHECK(CONTAINS(M[1], "\"pos\":[5000,0,0]") || SeqOf(M[1]) > 50);
	S.Stop();
}

static void TestRequestResync()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	CHECK(S.Start(C, nullptr));
	Game G;
	G.Tick(S, 1);
	TestClient A;
	CHECK(A.Connect(GPort));
	CHECK(A.WaitFor(2));
	// Static layer changed: everyone gets a fresh snapshot without a new delta.
	Rail R;
	R.Id = "new-track";
	R.Points = {{0, 0, 0}, {100, 0, 0}};
	G.Differ.SetRails({R});
	S.Publish(G.Differ.Snapshot(), std::string(), G.Differ.Seq());
	S.RequestResync();
	CHECK(A.WaitFor(3));
	auto M = A.Snapshot();
	CHECK(M.size() == 3 && IsType(M[2], "snapshot") && CONTAINS(M[2], "new-track"));
	CheckStream(M, 1);
	S.Stop();
}

static void TestHealth()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	CHECK(S.Start(C, nullptr));
	char Err[256] = {0};
	mg_connection* Conn = mg_download("127.0.0.1", GPort, 0, Err, sizeof(Err), "GET /health HTTP/1.0\r\nHost: localhost\r\n\r\n");
	CHECK(Conn != nullptr);
	if (Conn)
	{
		const mg_response_info* Info = mg_get_response_info(Conn);
		CHECK(Info && Info->status_code == 200);
		char Buf[256] = {0};
		int N = mg_read(Conn, Buf, sizeof(Buf) - 1);
		CHECK(N > 0);
		CHECK(std::strstr(Buf, "\"ok\":true") != nullptr);
		mg_close_connection(Conn);
	}
	S.Stop();
}

// GET a path; returns status, fills Body and the X-LiveMap-Version / Content-Type headers.
static int HttpGet(const char* Path, std::string* Body, std::string* Version = nullptr, std::string* Type = nullptr)
{
	char Err[256] = {0};
	mg_connection* Conn = mg_download("127.0.0.1", GPort, 0, Err, sizeof(Err), "GET %s HTTP/1.0\r\nHost: localhost\r\n\r\n", Path);
	if (!Conn)
	{
		return 0;
	}
	const mg_response_info* Info = mg_get_response_info(Conn);
	const int Status = Info ? Info->status_code : 0;
	for (int I = 0; Info && I < Info->num_headers; ++I)
	{
		if (Version && std::strcmp(Info->http_headers[I].name, "X-LiveMap-Version") == 0)
		{
			*Version = Info->http_headers[I].value;
		}
		if (Type && std::strcmp(Info->http_headers[I].name, "Content-Type") == 0)
		{
			*Type = Info->http_headers[I].value;
		}
	}
	Body->clear();
	char Buf[65536];
	for (int N; (N = mg_read(Conn, Buf, sizeof(Buf))) > 0;)
	{
		Body->append(Buf, size_t(N));
	}
	mg_close_connection(Conn);
	return Status;
}

static void TestStaticDocuments()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	CHECK(S.Start(C, nullptr));
	std::string Body, Version;
	CHECK_EQ(HttpGet("/static/buildings", &Body), 404);

	CHECK_EQ(S.SetStatic("buildings", "{\"a\":1}"), uint64_t(1));
	CHECK_EQ(S.SetStatic("lines", "{\"b\":2}"), uint64_t(2));
	CHECK_EQ(HttpGet("/static/buildings", &Body, &Version), 200);
	CHECK_EQ(Body, std::string("{\"a\":1}"));
	CHECK_EQ(Version, std::string("1"));
	CHECK_EQ(HttpGet("/static/lines", &Body, &Version), 200);
	CHECK_EQ(Version, std::string("2"));
	std::string Type;
	CHECK_EQ(HttpGet("/static/lines", &Body, nullptr, &Type), 200);
	CHECK_EQ(Type, std::string("application/json"));
	const std::string Png("\x89PNG\r\n\x1a\n\0\0", 10); // binary, with NULs
	S.SetStatic("map", Png, "image/png");
	CHECK_EQ(HttpGet("/static/map", &Body, nullptr, &Type), 200);
	CHECK_EQ(Type, std::string("image/png"));
	CHECK(Body == Png);
	CHECK_EQ(HttpGet("/static/nope", &Body), 404);
	CHECK_EQ(HttpGet("/static/", &Body), 404);

	// 8 MB body, replaced while clients download it (run under TSan/ASan).
	std::string Big(8 << 20, 'x');
	S.SetStatic("buildings", Big);
	std::atomic<bool> Stop{false};
	std::thread Writer([&] {
		while (!Stop)
		{
			S.SetStatic("buildings", std::string(8 << 20, 'y'));
			std::this_thread::sleep_for(5ms);
		}
	});
	for (int I = 0; I < 5; ++I)
	{
		std::string B;
		CHECK_EQ(HttpGet("/static/buildings", &B), 200);
		CHECK_EQ(B.size(), size_t(8 << 20));
		CHECK(B.find_first_not_of(B[0]) == std::string::npos); // never a torn mix
	}
	Stop = true;
	Writer.join();
	S.Stop();
}

static void TestPortInUse()
{
	Server A, B;
	ServerConfig C;
	C.Port = GPort;
	CHECK(A.Start(C, nullptr));
	std::string Err;
	CHECK(!B.Start(C, &Err));
	CHECK(!Err.empty());
	CHECK(!B.IsRunning());
	A.Stop();
}

static void TestRestart()
{
	Server S;
	ServerConfig C;
	C.Port = GPort;
	for (int I = 0; I < 3; ++I)
	{
		CHECK(S.Start(C, nullptr));
		TestClient A;
		CHECK(A.Connect(GPort));
		CHECK(A.WaitFor(2));
		S.Stop(); // with a client still connected
	}
}

static void TestStressChurnAndResync()
{
	// Game thread publishes as fast as it can while clients come and go.
	// MaxQueuedDeltas=4 forces the resync path.
	Server S;
	ServerConfig C;
	C.Port = GPort;
	C.MaxQueuedDeltas = 4;
	C.WorkerThreads = 24;
	CHECK(S.Start(C, nullptr));

	std::atomic<bool> Done{false};
	std::atomic<uint64_t> FinalSeq{0};
	std::thread GameThread([&] {
		Game G;
		for (int I = 1; I <= 3000; ++I)
		{
			G.Tick(S, I);
			if (I % 50 == 0)
			{
				std::this_thread::sleep_for(1ms);
			}
		}
		FinalSeq = G.Differ.Seq();
		Done = true;
	});

	std::vector<std::thread> Churners;
	for (int T = 0; T < 4; ++T)
	{
		Churners.emplace_back([T] {
			std::mt19937 Rng(T);
			for (int K = 0; K < 10; ++K)
			{
				TestClient X;
				if (X.Connect(GPort))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(Rng() % 20));
				}
			}
		});
	}

	TestClient Stayer;
	CHECK(Stayer.Connect(GPort));
	for (auto& T : Churners)
	{
		T.join();
	}
	GameThread.join();
	CHECK(Stayer.WaitForSeq(FinalSeq.load(), 10000ms));
	CheckStream(Stayer.Snapshot(), FinalSeq.load());
	std::printf("  stress: final seq %llu, resyncs %llu, stayer got %zu messages\n",
		(unsigned long long)FinalSeq.load(), (unsigned long long)S.ResyncCount(), Stayer.Snapshot().size());
	S.Stop();
}

int main()
{
	mg_init_library(0);
	struct { const char* Name; void (*Fn)(); } Tests[] = {
		{"handshake_and_deltas", TestHandshakeAndDeltas},
		{"late_joiner", TestLateJoinerGetsNoStaleDeltas},
		{"request_resync", TestRequestResync},
		{"health", TestHealth},
		{"static_documents", TestStaticDocuments},
		{"port_in_use", TestPortInUse},
		{"restart", TestRestart},
		{"stress_churn_resync", TestStressChurnAndResync},
	};
	for (auto& T : Tests)
	{
		const int Before = GFailures;
		T.Fn();
		std::printf("%s %s\n", GFailures == Before ? "ok  " : "FAIL", T.Name);
		++GPort; // avoid TIME_WAIT surprises between tests
	}
	mg_exit_library();
	std::printf("%s (%d failures)\n", GFailures ? "FAIL" : "OK", GFailures);
	return GFailures ? 1 : 0;
}
