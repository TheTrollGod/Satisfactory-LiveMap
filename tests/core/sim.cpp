// Fake game: publishes players, trains and trucks moving around at TickHz,
// plus a static factory (foundations, machines, belts) that grows a machine
// every 5 s. Used for end-to-end testing the frontend without Satisfactory.
//   livemap_sim [port] [seconds] [foundations]
#include "LiveMapCore.h"
#include "LiveMapServer.h"

#include <algorithm>
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

		if (Tick > 0 && Tick % (C.TickHz * 5) == 0)
		{
			Factory.Items.push_back({Machines % 2 ? 2u : 1u, FX + 1200.0 * Machines, FY + 400, 400, 15.0 * Machines});
			++Machines;
			PublishFactory();
			S.RequestResync();
		}

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
