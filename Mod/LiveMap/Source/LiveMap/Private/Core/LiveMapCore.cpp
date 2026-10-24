#include "LiveMapCore.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace livemap
{
	const char* LayerName(ELayer Layer)
	{
		switch (Layer)
		{
		case ELayer::Players: return "players";
		case ELayer::Trains: return "trains";
		case ELayer::Vehicles: return "vehicles";
		default: return "unknown";
		}
	}

	// --- JsonWriter -------------------------------------------------------

	int64_t JsonWriter::Round(double V)
	{
		if (!std::isfinite(V))
		{
			return 0;
		}
		return static_cast<int64_t>(std::llround(V));
	}

	void JsonWriter::Sep()
	{
		if (AfterKey)
		{
			AfterKey = false;
			return;
		}
		if (!First.empty())
		{
			if (!First.back())
			{
				Out += ',';
			}
			First.back() = false;
		}
	}

	void JsonWriter::BeginObject() { Sep(); Out += '{'; First.push_back(true); }
	void JsonWriter::EndObject() { Out += '}'; First.pop_back(); }
	void JsonWriter::BeginArray() { Sep(); Out += '['; First.push_back(true); }
	void JsonWriter::EndArray() { Out += ']'; First.pop_back(); }

	void JsonWriter::Key(const char* K)
	{
		Sep();
		Out += '"';
		Out += K;
		Out += "\":";
		AfterKey = true;
	}

	void JsonWriter::String(const std::string& S)
	{
		Sep();
		Out += '"';
		for (const char Ch : S)
		{
			const unsigned char C = static_cast<unsigned char>(Ch);
			switch (C)
			{
			case '"': Out += "\\\""; break;
			case '\\': Out += "\\\\"; break;
			case '\n': Out += "\\n"; break;
			case '\r': Out += "\\r"; break;
			case '\t': Out += "\\t"; break;
			default:
				if (C < 0x20)
				{
					static const char Hex[] = "0123456789abcdef";
					Out += "\\u00";
					Out += Hex[C >> 4];
					Out += Hex[C & 0xF];
				}
				else
				{
					Out += static_cast<char>(C);
				}
			}
		}
		Out += '"';
	}

	void JsonWriter::Int(int64_t V)
	{
		Sep();
		// to_chars is locale-independent, unlike snprintf("%f") which honours LC_NUMERIC.
		char Buf[24];
		const std::to_chars_result R = std::to_chars(Buf, Buf + sizeof(Buf), V);
		Out.append(Buf, static_cast<size_t>(R.ptr - Buf));
	}

	void JsonWriter::Raw(const char* S)
	{
		Sep();
		Out += S;
	}

	void WriteEntity(JsonWriter& W, const Entity& E)
	{
		W.BeginObject();
		W.Key("id"); W.String(E.Id);
		if (!E.Name.empty()) { W.Key("name"); W.String(E.Name); }
		if (!E.Kind.empty()) { W.Key("kind"); W.String(E.Kind); }
		W.Key("pos");
		W.BeginArray(); W.Number(E.X); W.Number(E.Y); W.Number(E.Z); W.EndArray();
		W.Key("yaw"); W.Number(E.Yaw);
		if (E.Speed != 0) { W.Key("speed"); W.Number(E.Speed); }
		W.EndObject();
	}

	// --- FrameDiffer ------------------------------------------------------

	static double AngleDelta(double A, double B)
	{
		double D = std::fmod(std::fabs(A - B), 360.0);
		return D > 180.0 ? 360.0 - D : D;
	}

	bool FrameDiffer::Changed(const Entity& A, const Entity& B) const
	{
		const double DX = A.X - B.X, DY = A.Y - B.Y, DZ = A.Z - B.Z;
		return DX * DX + DY * DY + DZ * DZ > PositionEpsilon * PositionEpsilon
			|| AngleDelta(A.Yaw, B.Yaw) > YawEpsilon
			|| A.Name != B.Name
			|| A.Kind != B.Kind
			|| JsonWriter::Round(A.Speed) != JsonWriter::Round(B.Speed);
	}

	std::string FrameDiffer::Update(const Frame& Next)
	{
		struct LayerDelta
		{
			std::vector<const Entity*> Upserts;
			std::vector<std::string> Removed;
		};
		LayerDelta Deltas[static_cast<int>(ELayer::Count)];
		bool Any = false;

		for (int L = 0; L < static_cast<int>(ELayer::Count); ++L)
		{
			auto& Known = State[L];
			std::unordered_map<std::string, const Entity*> Seen;
			Seen.reserve(Next.Layers[L].size());

			for (const Entity& E : Next.Layers[L])
			{
				if (!Seen.emplace(E.Id, &E).second)
				{
					continue; // duplicate id in one frame: first wins
				}
				auto It = Known.find(E.Id);
				if (It == Known.end() || Changed(It->second, E))
				{
					Deltas[L].Upserts.push_back(&E);
				}
			}
			for (const auto& [Id, Old] : Known)
			{
				if (!Seen.count(Id))
				{
					Deltas[L].Removed.push_back(Id);
				}
			}
			// Deterministic output order makes tests and debugging sane.
			std::sort(Deltas[L].Removed.begin(), Deltas[L].Removed.end());

			for (const Entity* E : Deltas[L].Upserts)
			{
				Known[E->Id] = *E;
			}
			for (const std::string& Id : Deltas[L].Removed)
			{
				Known.erase(Id);
			}
			Any |= !Deltas[L].Upserts.empty() || !Deltas[L].Removed.empty();
		}

		if (!Any)
		{
			return std::string();
		}

		++SeqCounter;
		JsonWriter W;
		W.BeginObject();
		W.Key("type"); W.String("delta");
		W.Key("seq"); W.Int(static_cast<int64_t>(SeqCounter));
		for (int L = 0; L < static_cast<int>(ELayer::Count); ++L)
		{
			if (Deltas[L].Upserts.empty())
			{
				continue;
			}
			W.Key(LayerName(static_cast<ELayer>(L)));
			W.BeginArray();
			for (const Entity* E : Deltas[L].Upserts)
			{
				WriteEntity(W, *E);
			}
			W.EndArray();
		}
		bool AnyRemoved = false;
		for (const LayerDelta& D : Deltas)
		{
			AnyRemoved |= !D.Removed.empty();
		}
		if (AnyRemoved)
		{
			W.Key("removed");
			W.BeginObject();
			for (int L = 0; L < static_cast<int>(ELayer::Count); ++L)
			{
				if (Deltas[L].Removed.empty())
				{
					continue;
				}
				W.Key(LayerName(static_cast<ELayer>(L)));
				W.BeginArray();
				for (const std::string& Id : Deltas[L].Removed)
				{
					W.String(Id);
				}
				W.EndArray();
			}
			W.EndObject();
		}
		W.EndObject();
		return W.Take();
	}

	void FrameDiffer::SetRails(const std::vector<Rail>& InRails)
	{
		JsonWriter W;
		W.BeginArray();
		for (const Rail& R : InRails)
		{
			W.BeginObject();
			W.Key("id"); W.String(R.Id);
			W.Key("points");
			W.BeginArray();
			for (const auto& P : R.Points)
			{
				W.BeginArray(); W.Number(P[0]); W.Number(P[1]); W.Number(P[2]); W.EndArray();
			}
			W.EndArray();
			W.EndObject();
		}
		W.EndArray();
		RailsJson = W.Take();
	}

	std::string FrameDiffer::Snapshot() const
	{
		JsonWriter W;
		W.BeginObject();
		W.Key("type"); W.String("snapshot");
		W.Key("seq"); W.Int(static_cast<int64_t>(SeqCounter));
		for (int L = 0; L < static_cast<int>(ELayer::Count); ++L)
		{
			std::vector<const Entity*> Sorted;
			Sorted.reserve(State[L].size());
			for (const auto& [Id, E] : State[L])
			{
				Sorted.push_back(&E);
			}
			std::sort(Sorted.begin(), Sorted.end(), [](const Entity* A, const Entity* B) { return A->Id < B->Id; });

			W.Key(LayerName(static_cast<ELayer>(L)));
			W.BeginArray();
			for (const Entity* E : Sorted)
			{
				WriteEntity(W, *E);
			}
			W.EndArray();
		}
		W.Key("rails");
		W.Raw(RailsJson.c_str());
		W.Key("static");
		W.BeginObject();
		for (const auto& [Name, Version] : StaticVersions)
		{
			W.Key(Name.c_str());
			W.Int(static_cast<int64_t>(Version));
		}
		W.EndObject();
		W.EndObject();
		return W.Take();
	}

	// --- Static layers ----------------------------------------------------

	namespace
	{
		// FNV-style mix over the rounded values, so sub-centimetre jitter doesn't
		// count as a change. Word-at-a-time: this runs on the game thread.
		struct Fnv
		{
			uint64_t H = 1469598103934665603ull;
			void Add(int64_t V)
			{
				H ^= static_cast<uint64_t>(V);
				H *= 1099511628211ull;
				H ^= H >> 29;
			}
			void Add(const std::string& S)
			{
				for (const char C : S)
				{
					H ^= static_cast<unsigned char>(C);
					H *= 1099511628211ull;
				}
				Add(static_cast<int64_t>(S.size()));
			}
		};
	}

	uint64_t HashBuildings(const BuildingSet& Set)
	{
		Fnv F;
		for (const BuildingClass& C : Set.Classes)
		{
			F.Add(C.Name); F.Add(C.Display); F.Add(C.Category);
			F.Add(JsonWriter::Round(C.MinX)); F.Add(JsonWriter::Round(C.MinY));
			F.Add(JsonWriter::Round(C.MaxX)); F.Add(JsonWriter::Round(C.MaxY));
		}
		for (const PlacedBuilding& B : Set.Items)
		{
			F.Add(B.Class);
			F.Add(JsonWriter::Round(B.X)); F.Add(JsonWriter::Round(B.Y));
			F.Add(JsonWriter::Round(B.Z)); F.Add(JsonWriter::Round(B.Yaw));
		}
		return F.H;
	}

	uint64_t HashLines(const LineSet& Lines)
	{
		Fnv F;
		for (const auto& [Kind, Group] : Lines)
		{
			F.Add(Kind);
			for (const Polyline& L : Group)
			{
				F.Add(static_cast<int64_t>(L.size()));
				for (const auto& P : L)
				{
					F.Add(JsonWriter::Round(P[0])); F.Add(JsonWriter::Round(P[1]));
				}
			}
		}
		return F.H;
	}

	std::string EncodeBuildings(const BuildingSet& Set)
	{
		JsonWriter W;
		W.Reserve(Set.Items.size() * 32 + Set.Classes.size() * 96 + 64);
		W.BeginObject();
		W.Key("classes");
		W.BeginArray();
		for (const BuildingClass& C : Set.Classes)
		{
			W.BeginObject();
			W.Key("name"); W.String(C.Name);
			if (!C.Display.empty()) { W.Key("display"); W.String(C.Display); }
			W.Key("cat"); W.String(C.Category);
			W.Key("box");
			W.BeginArray(); W.Number(C.MinX); W.Number(C.MinY); W.Number(C.MaxX); W.Number(C.MaxY); W.EndArray();
			W.EndObject();
		}
		W.EndArray();

		std::vector<const PlacedBuilding*> Sorted;
		Sorted.reserve(Set.Items.size());
		for (const PlacedBuilding& B : Set.Items)
		{
			if (B.Class < Set.Classes.size())
			{
				Sorted.push_back(&B);
			}
		}
		// Stable so equal heights keep collection order and the output (and its
		// hash) only changes when the world does.
		std::stable_sort(Sorted.begin(), Sorted.end(),
			[](const PlacedBuilding* A, const PlacedBuilding* B) { return JsonWriter::Round(A->Z) < JsonWriter::Round(B->Z); });

		W.Key("stride"); W.Int(5);
		W.Key("items");
		W.BeginArray();
		for (const PlacedBuilding* B : Sorted)
		{
			W.Int(B->Class); W.Number(B->X); W.Number(B->Y); W.Number(B->Z); W.Number(B->Yaw);
		}
		W.EndArray();
		W.EndObject();
		return W.Take();
	}

	std::string EncodeLines(const LineSet& Lines)
	{
		JsonWriter W;
		W.BeginObject();
		for (const auto& [Kind, Group] : Lines)
		{
			W.Key(Kind.c_str());
			W.BeginArray();
			for (const Polyline& L : Group)
			{
				if (L.size() < 2)
				{
					continue;
				}
				W.BeginArray();
				for (const auto& P : L)
				{
					W.Number(P[0]); W.Number(P[1]);
				}
				W.EndArray();
			}
			W.EndArray();
		}
		W.EndObject();
		return W.Take();
	}
}
