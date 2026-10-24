// Fake game: publishes players, trains and trucks moving around at TickHz.
// Used for end-to-end testing the frontend without Satisfactory.
//   livemap_sim [port] [seconds]
#include "LiveMapCore.h"
#include "LiveMapServer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace livemap;

int main(int Argc, char** Argv)
{
	ServerConfig C;
	C.Port = Argc > 1 ? std::atoi(Argv[1]) : 8086;
	const int Seconds = Argc > 2 ? std::atoi(Argv[2]) : 0;

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

		std::string Delta = D.Update(F);
		S.Publish(D.Snapshot(), std::move(Delta), D.Seq());

		if (Seconds && std::chrono::steady_clock::now() - Start > std::chrono::seconds(Seconds))
		{
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1000 / C.TickHz));
	}
	S.Stop();
	return 0;
}
