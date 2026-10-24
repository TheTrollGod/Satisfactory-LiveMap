// Unit tests for FrameDiffer and JsonWriter. Run with --dump to print every
// generated message, one per line, for an external JSON validity check.
#include "LiveMapCore.h"
#include "check.h"

#include <chrono>
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
	CHECK_EQ(S, std::string("{\"type\":\"snapshot\",\"seq\":0,\"players\":[],\"trains\":[],\"vehicles\":[],\"rails\":[]}"));
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
	if (!GDump)
	{
		std::printf("%s (%d failures)\n", GFailures ? "FAIL" : "OK", GFailures);
	}
	return GFailures ? 1 : 0;
}
