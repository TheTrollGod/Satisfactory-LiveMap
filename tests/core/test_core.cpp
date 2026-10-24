// Unit tests for FrameDiffer and JsonWriter. Run with --dump to print every
// generated message, one per line, for an external JSON validity check.
#include "LiveMapCore.h"
#include "check.h"

#include <chrono>
#include <random>
#include <cmath>
#include <cstring>
#include <limits>

using namespace livemap;

static bool GDump = false;
static void Dump(const std::string& S)
{
	if (GDump && !S.empty())
	{
		std::printf("%s\n", S.c_str());
	}
}

static Entity Make(const char* Id, double X, double Y, double Yaw = 0)
{
	Entity E;
	E.Id = Id;
	E.Name = Id;
	E.X = X;
	E.Y = Y;
	E.Yaw = Yaw;
	return E;
}

static void TestFirstFrameSendsEverything()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 100, 200));
	F[ELayer::Trains].push_back(Make("t1", -5, 0));
	const std::string Delta = D.Update(F);
	Dump(Delta);
	CHECK_EQ(D.Seq(), 1u);
	CHECK(CONTAINS(Delta, "\"type\":\"delta\""));
	CHECK(CONTAINS(Delta, "\"seq\":1"));
	CHECK(CONTAINS(Delta, "\"players\":[{\"id\":\"p1\""));
	CHECK(CONTAINS(Delta, "\"pos\":[-5,0,0]"));
	CHECK(!CONTAINS(Delta, "vehicles"));
	CHECK(!CONTAINS(Delta, "removed"));
}

static void TestUnchangedFrameIsEmpty()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 100, 200));
	D.Update(F);
	CHECK(D.Update(F).empty());
	CHECK_EQ(D.Seq(), 1u);

	// Below epsilon (10 cm) is not a change.
	F[ELayer::Players][0].X += 5;
	CHECK(D.Update(F).empty());
	CHECK_EQ(D.Seq(), 1u);
}

static void TestOnlyMovedEntitiesAreSent()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 0, 0));
	F[ELayer::Players].push_back(Make("p2", 0, 0));
	D.Update(F);
	F[ELayer::Players][1].X = 500;
	const std::string Delta = D.Update(F);
	Dump(Delta);
	CHECK(CONTAINS(Delta, "\"id\":\"p2\""));
	CHECK(!CONTAINS(Delta, "\"id\":\"p1\""));
	CHECK_EQ(D.Seq(), 2u);
}

static void TestRemoval()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Vehicles].push_back(Make("v2", 0, 0));
	F[ELayer::Vehicles].push_back(Make("v1", 0, 0));
	D.Update(F);
	F[ELayer::Vehicles].clear();
	const std::string Delta = D.Update(F);
	Dump(Delta);
	CHECK(CONTAINS(Delta, "\"removed\":{\"vehicles\":[\"v1\",\"v2\"]}"));
	CHECK(D.Update(F).empty());
	CHECK(CONTAINS(D.Snapshot(), "\"vehicles\":[]"));
}

static void TestYawWraparound()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 0, 0, 359.8));
	D.Update(F);
	F[ELayer::Players][0].Yaw = -0.1; // 0.1 degrees away, across the wrap
	CHECK(D.Update(F).empty());
	F[ELayer::Players][0].Yaw = 90;
	CHECK(!D.Update(F).empty());
}

static void TestDuplicateIdFirstWins()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 1000, 0));
	F[ELayer::Players].push_back(Make("p1", 9000, 0));
	const std::string Delta = D.Update(F);
	CHECK(CONTAINS(Delta, "\"pos\":[1000,0,0]"));
	CHECK(!CONTAINS(Delta, "9000"));
}

static void TestSnapshotIsSortedAndComplete()
{
	FrameDiffer D;
	Frame F;
	F[ELayer::Players].push_back(Make("b", 0, 0));
	F[ELayer::Players].push_back(Make("a", 0, 0));
	D.Update(F);
	Rail R;
	R.Id = "r1";
	R.Points = {{0, 0, 0}, {100.4, -200.6, 3}};
	D.SetRails({R});
	const std::string S = D.Snapshot();
	Dump(S);
	CHECK(CONTAINS(S, "\"type\":\"snapshot\",\"seq\":1"));
	CHECK(S.find("\"id\":\"a\"") < S.find("\"id\":\"b\""));
	CHECK(CONTAINS(S, "\"rails\":[{\"id\":\"r1\",\"points\":[[0,0,0],[100,-201,3]]}]"));
	CHECK(CONTAINS(S, "\"trains\":[]"));
}

static void TestEmptySnapshot()
{
	FrameDiffer D;
	const std::string S = D.Snapshot();
	Dump(S);
	CHECK_EQ(S, std::string("{\"type\":\"snapshot\",\"seq\":0,\"players\":[],\"trains\":[],\"vehicles\":[],\"rails\":[],\"static\":{}}"));
}

static void TestJsonEscaping()
{
	JsonWriter W;
	W.BeginObject();
	W.Key("s");
	W.String(std::string("quote\" back\\ nl\n tab\t ctl\x01 utf8 \xC3\xA9 end"));
	W.EndObject();
	Dump(W.Str());
	CHECK_EQ(W.Str(), std::string("{\"s\":\"quote\\\" back\\\\ nl\\n tab\\t ctl\\u0001 utf8 \xC3\xA9 end\"}"));
}

static void TestNumbers()
{
	JsonWriter W;
	W.BeginArray();
	W.Number(-0.4);
	W.Number(-0.6);
	W.Number(2.5);
	W.Number(std::numeric_limits<double>::quiet_NaN());
	W.Number(std::numeric_limits<double>::infinity());
	W.Int(std::numeric_limits<int64_t>::min());
	W.Int(425301);
	W.EndArray();
	Dump(W.Str());
	CHECK_EQ(W.Str(), std::string("[0,-1,3,0,0,-9223372036854775808,425301]"));
}

static void TestNestedSeparators()
{
	JsonWriter W;
	W.BeginObject();
	W.Key("a"); W.BeginArray(); W.BeginArray(); W.Int(1); W.Int(2); W.EndArray(); W.BeginArray(); W.EndArray(); W.EndArray();
	W.Key("b"); W.BeginObject(); W.EndObject();
	W.Key("c"); W.Raw("true");
	W.EndObject();
	Dump(W.Str());
	CHECK_EQ(W.Str(), std::string("{\"a\":[[1,2],[]],\"b\":{},\"c\":true}"));
}

static void TestManyEntities()
{
	// Big-save sanity: 20k entities, then move 1%.
	FrameDiffer D;
	Frame F;
	for (int I = 0; I < 20000; ++I)
	{
		F[ELayer::Vehicles].push_back(Make(("v" + std::to_string(I)).c_str(), I * 100.0, 0));
	}
	D.Update(F);
	for (int I = 0; I < 20000; I += 100)
	{
		F[ELayer::Vehicles][I].Y += 1000;
	}
	const std::string Delta = D.Update(F);
	size_t Count = 0;
	for (size_t P = Delta.find("\"id\""); P != std::string::npos; P = Delta.find("\"id\"", P + 1))
	{
		++Count;
	}
	CHECK_EQ(Count, 200u);
}

static void TestSnapshotCostWithManyRails()
{
	// 5000 track segments x 12 points. Snapshot() runs every tick on the game
	// thread, so rails must be pre-encoded, not re-encoded each time.
	FrameDiffer D;
	std::vector<Rail> Rails(5000);
	for (size_t I = 0; I < Rails.size(); ++I)
	{
		Rails[I].Id = "track-" + std::to_string(I);
		for (int P = 0; P < 12; ++P)
		{
			Rails[I].Points.push_back({double(I), double(P), 0});
		}
	}
	D.SetRails(Rails);
	Frame F;
	F[ELayer::Players].push_back(Make("p1", 0, 0));
	D.Update(F);
	const auto T0 = std::chrono::steady_clock::now();
	size_t Bytes = 0;
	for (int I = 0; I < 100; ++I)
	{
		Bytes = D.Snapshot().size();
	}
	const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count() / 100;
	CHECK(CONTAINS(D.Snapshot(), "\"id\":\"track-4999\""));
	if (!GDump)
	{
		std::printf("  snapshot with 5000 rails: %zu KB, %.2f ms each\n", Bytes / 1024, Ms);
	}
}

static void TestSnapshotListsStaticVersions()
{
	FrameDiffer D;
	D.SetStaticVersion("lines", 4);
	D.SetStaticVersion("buildings", 3);
	D.SetStaticVersion("lines", 5);
	const std::string S = D.Snapshot();
	Dump(S);
	CHECK(CONTAINS(S, "\"static\":{\"buildings\":3,\"lines\":5}}"));
}

static void TestEncodeBuildings()
{
	BuildingSet Set;
	Set.Classes.push_back({"Build_Foundation_8x4_C", "Foundation 8m x 4m", "structure", -400, -400, 400, 400});
	Set.Classes.push_back({"Build_ConstructorMk1_C", "", "production", -390.4, -500, 390.4, 500.6});
	Set.Items.push_back({1, 1000.4, -2000.6, 800, 90});
	Set.Items.push_back({0, 0, 0, 0, 0});
	Set.Items.push_back({7, 1, 1, 1, 1}); // bad class index: dropped
	Set.Items.push_back({0, 800, 0, 0, -90});
	const std::string S = EncodeBuildings(Set);
	Dump(S);
	CHECK_EQ(S, std::string(
		"{\"classes\":["
		"{\"name\":\"Build_Foundation_8x4_C\",\"display\":\"Foundation 8m x 4m\",\"cat\":\"structure\",\"box\":[-400,-400,400,400]},"
		"{\"name\":\"Build_ConstructorMk1_C\",\"cat\":\"production\",\"box\":[-390,-500,390,501]}],"
		"\"stride\":5,\"items\":[0,0,0,0,0,0,800,0,0,-90,1,1000,-2001,800,90]}"));
}

static void TestEncodeLines()
{
	LineSet L;
	L["pipes"].push_back({{0, 0}, {100, 0}, {100, 100}});
	L["belts"].push_back({{1.5, -2.5}, {3, 4}});
	L["belts"].push_back({{9, 9}}); // single point: dropped
	L["hypertubes"];
	const std::string S = EncodeLines(L);
	Dump(S);
	CHECK(HashLines(L) != HashLines(LineSet{}));
	CHECK_EQ(S, std::string("{\"belts\":[[2,-3,3,4]],\"hypertubes\":[],\"pipes\":[[0,0,100,0,100,100]]}"));
}

static void TestBuildingsEncodeCost()
{
	// A late-game save: ~200k pieces, most of them lightweight foundations.
	// Hashing runs on the game thread every rescan; encoding on the thread pool.
	BuildingSet Set;
	for (int C = 0; C < 300; ++C)
	{
		Set.Classes.push_back({"Build_Class_" + std::to_string(C) + "_C", "Class", "structure", -400, -400, 400, 400});
	}
	std::mt19937 Rng(42);
	std::uniform_real_distribution<double> Pos(-375000, 375000);
	for (int I = 0; I < 200000; ++I)
	{
		Set.Items.push_back({uint32_t(Rng() % 300), Pos(Rng), Pos(Rng), double(Rng() % 4000), double(Rng() % 360)});
	}
	const auto T0 = std::chrono::steady_clock::now();
	const uint64_t H = HashBuildings(Set);
	const auto T1 = std::chrono::steady_clock::now();
	const std::string S = EncodeBuildings(Set);
	const auto T2 = std::chrono::steady_clock::now();
	CHECK(S.size() > 200000u * 10);
	CHECK(S.size() <= 200000u * 32 + 300 * 96 + 64); // the reserve() estimate holds
	CHECK_EQ(HashBuildings(Set), H);
	Set.Items[123].X += 0.2; // rounds to the same cm
	CHECK_EQ(HashBuildings(Set), H);
	Set.Items[123].X += 5;
	CHECK(HashBuildings(Set) != H);
	if (!GDump)
	{
		using Ms = std::chrono::duration<double, std::milli>;
		std::printf("  buildings with 200k items: %zu KB, hash %.1f ms, encode %.1f ms\n", S.size() / 1024,
			Ms(T1 - T0).count(), Ms(T2 - T1).count());
	}
}

int main(int Argc, char** Argv)
{
	GDump = Argc > 1 && std::strcmp(Argv[1], "--dump") == 0;
	TestFirstFrameSendsEverything();
	TestUnchangedFrameIsEmpty();
	TestOnlyMovedEntitiesAreSent();
	TestRemoval();
	TestYawWraparound();
	TestDuplicateIdFirstWins();
	TestSnapshotIsSortedAndComplete();
	TestEmptySnapshot();
	TestJsonEscaping();
	TestNumbers();
	TestNestedSeparators();
	TestManyEntities();
	TestSnapshotCostWithManyRails();
	TestSnapshotListsStaticVersions();
	TestEncodeBuildings();
	TestEncodeLines();
	TestBuildingsEncodeCost();
	if (!GDump)
	{
		std::printf("%s (%d failures)\n", GFailures ? "FAIL" : "OK", GFailures);
	}
	return GFailures ? 1 : 0;
}
