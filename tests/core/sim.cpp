// Fake game: publishes players, trains and trucks moving around at TickHz,
// plus a static factory (foundations, machines, belts) that grows a machine
// every 5 s. Used for end-to-end testing the frontend without Satisfactory.
//   livemap_sim [port] [seconds] [foundations]
#include "LiveMapCore.h"
#include "LiveMapServer.h"
#include "WebAssets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace livemap;

// Minimal PNG writer (RGB, zlib "stored" blocks) so the sim can serve a
// terrain image without pulling in an image library.
static uint32_t Crc(const unsigned char* P, size_t N, uint32_t C = 0xFFFFFFFFu)
{
	for (size_t I = 0; I < N; ++I)
	{
		C ^= P[I];
		for (int K = 0; K < 8; ++K)
		{
			C = (C >> 1) ^ (0xEDB88320u & (0u - (C & 1u)));
		}
	}
	return C;
}

static std::string MakePng(int W, int H)
{
	std::string Raw;
	for (int Y = 0; Y < H; ++Y)
	{
		Raw += '\0'; // filter: none
		for (int X = 0; X < W; ++X)
		{
			Raw += char(40 + X * 60 / W);
			Raw += char(60 + Y * 80 / H);
			Raw += char(50);
		}
	}
	std::string Z = "\x78\x01";
	uint32_t A = 1, B = 0;
	for (const char Ch : Raw)
	{
		A = (A + static_cast<unsigned char>(Ch)) % 65521;
		B = (B + A) % 65521;
	}
	for (size_t Off = 0; Off < Raw.size(); Off += 65535)
	{
		const size_t N = std::min<size_t>(65535, Raw.size() - Off);
		Z += char(Off + N == Raw.size() ? 1 : 0);
		Z += char(N & 0xFF); Z += char(N >> 8);
		Z += char(~N & 0xFF); Z += char((~N >> 8) & 0xFF);
		Z.append(Raw, Off, N);
	}
	const uint32_t Adler = (B << 16) | A;
	for (int S = 24; S >= 0; S -= 8) Z += char(Adler >> S);

	std::string Out("\x89PNG\r\n\x1a\n", 8);
	auto Chunk = [&Out](const char* Type, const std::string& Data) {
		for (int S = 24; S >= 0; S -= 8) Out += char(Data.size() >> S);
		std::string Body = std::string(Type, 4) + Data;
		Out += Body;
		const uint32_t C = ~Crc(reinterpret_cast<const unsigned char*>(Body.data()), Body.size());
		for (int S = 24; S >= 0; S -= 8) Out += char(C >> S);
	};
	std::string Ihdr;
	for (const int V : {W, H})
	{
		for (int S = 24; S >= 0; S -= 8) Ihdr += char(V >> S);
	}
	Ihdr += std::string("\x08\x02\x00\x00\x00", 5); // 8-bit RGB
	Chunk("IHDR", Ihdr);
	Chunk("IDAT", Z);
	Chunk("IEND", std::string());
	return Out;
}

int main(int Argc, char** Argv)
{
	ServerConfig C;
	C.Port = Argc > 1 ? std::atoi(Argv[1]) : 8086;
	C.WebFiles = GetWebFiles(C.WebFileCount); // the map page, if web/dist was embedded
	const int Seconds = Argc > 2 ? std::atoi(Argv[2]) : 0;
	const int Foundations = Argc > 3 ? std::atoi(Argv[3]) : 2000;

	Server S;
	std::string Err;
	if (!S.Start(C, &Err))
	{
		std::fprintf(stderr, "start failed: %s\n", Err.c_str());
		return 1;
	}
	std::printf("listening on %d\n", C.Port);
	std::fflush(stdout);

	FrameDiffer D;
	// One loop of track around the map centre (Unreal cm).
	const double CX = 50301.83, CY = 0, R = 150000;
	Rail Loop;
	Loop.Id = "rail-loop";
	for (int I = 0; I <= 64; ++I)
	{
		const double A = I * 2 * M_PI / 64;
		Loop.Points.push_back({CX + R * std::cos(A), CY + R * std::sin(A), 0});
	}
	D.SetRails({Loop});

	// Factory: a square slab of 8 m foundations east of the centre, machines on top.
	BuildingSet Factory;
	Factory.Classes.push_back({"Build_Foundation_8x4_01_C", "Foundation 8m x 4m", "structure", -400, -400, 400, 400});
	Factory.Classes.push_back({"Build_ConstructorMk1_C", "Constructor", "production", -390, -500, 390, 500});
	Factory.Classes.push_back({"Build_StorageContainerMk1_C", "Storage Container", "storage", -250, -500, 250, 500});
	const int Side = std::max(1, int(std::sqrt(double(Foundations))));
	const double FX = CX + 200000, FY = CY;
	for (int I = 0; I < Foundations; ++I)
	{
		Factory.Items.push_back({0, FX + (I % Side) * 800.0, FY + (I / Side) * 800.0, 0, 0});
	}
	LineSet Lines;
	for (int I = 0; I < 10; ++I)
	{
		Lines["belts"].push_back({{FX, FY + 1600.0 * I}, {FX + Side * 800.0, FY + 1600.0 * I}});
	}
	Lines["pipes"].push_back({{FX, FY - 2000}, {FX + Side * 800.0, FY - 2000}});
	int Machines = 0;
	auto PublishFactory = [&] {
		D.SetStaticVersion("buildings", S.SetStatic("buildings", EncodeBuildings(Factory)));
		D.SetStaticVersion("lines", S.SetStatic("lines", EncodeLines(Lines)));
	};
	PublishFactory();
	D.SetStaticVersion("map", S.SetStatic("map", MakePng(256, 256), "image/png"));

	const auto Start = std::chrono::steady_clock::now();
	for (int Tick = 0;; ++Tick)
	{
		const double T = Tick / double(C.TickHz);
		Frame F;
		Entity P;
		P.Id = "player-1"; P.Name = "Pioneer \"One\""; P.Kind = "player";
		P.X = CX + 40000 * std::cos(T * 0.3); P.Y = CY + 40000 * std::sin(T * 0.3); P.Yaw = std::fmod(T * 20, 360);
		F[ELayer::Players].push_back(P);
		// Second player disconnects every 10 s, to exercise "removed".
		if (int(T) % 20 < 10)
		{
			Entity P2 = P;
			P2.Id = "player-2"; P2.Name = "Ficsit Intern";
			P2.X = CX - 100000; P2.Y = CY + 1000 * T;
			F[ELayer::Players].push_back(P2);
		}
		Entity Tr;
		Tr.Id = "train-1"; Tr.Name = "Iron Express"; Tr.Kind = "train";
		Tr.X = CX + R * std::cos(T * 0.1); Tr.Y = CY + R * std::sin(T * 0.1); Tr.Speed = 120;
		F[ELayer::Trains].push_back(Tr);
		Entity V;
		V.Id = "truck-1"; V.Name = "Truck"; V.Kind = "Truck";
		V.X = CX + 80000 * std::sin(T * 0.2); V.Y = CY - 60000;
		F[ELayer::Vehicles].push_back(V);

		const bool bGrow = Tick > 0 && Tick % (C.TickHz * 5) == 0;
		if (bGrow)
		{
			Factory.Items.push_back({Machines % 2 ? 2u : 1u, FX + 1200.0 * Machines, FY + 400, 400, 15.0 * Machines});
			++Machines;
			PublishFactory();
		}

		std::string Delta = D.Update(F);
		S.Publish(D.Snapshot(), std::move(Delta), D.Seq());
		if (bGrow)
		{
			S.RequestResync(); // after Publish, so the resync carries the new versions
		}

		if (Seconds && std::chrono::steady_clock::now() - Start > std::chrono::seconds(Seconds))
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1000 / C.TickHz));
	}
	S.Stop();
	return 0;
}
