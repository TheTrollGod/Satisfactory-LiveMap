#include "LiveMapCore.h"

#include <algorithm>
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
		// Locale-independent on purpose: snprintf("%f") honours LC_NUMERIC.
		char Buf[24];
		int N = 0;
		uint64_t U = V < 0 ? 0 - static_cast<uint64_t>(V) : static_cast<uint64_t>(V);
		do
		{
			Buf[N++] = static_cast<char>('0' + U % 10);
			U /= 10;
		} while (U);
		if (V < 0)
		{
			Out += '-';
		}
		while (N)
		{
			Out += Buf[--N];
		}
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
		W.EndObject();
		return W.Take();
	}
}
