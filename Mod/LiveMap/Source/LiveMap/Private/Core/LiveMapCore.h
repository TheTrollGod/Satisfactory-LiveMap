#pragma once

// Engine-free core: data types, diffing and JSON encoding.
// No UObjects, no exceptions, no RTTI, so it builds inside UBT and in the
// standalone test harness under tests/core.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace livemap
{
	struct Entity
	{
		std::string Id;
		std::string Name;
		std::string Kind;
		double X = 0, Y = 0, Z = 0;
		double Yaw = 0;
		double Speed = 0;
	};

	struct Rail
	{
		std::string Id;
		std::vector<std::array<double, 3>> Points;
	};

	enum class ELayer : int { Players, Trains, Vehicles, Count };

	const char* LayerName(ELayer Layer);

	struct Frame
	{
		std::vector<Entity> Layers[static_cast<int>(ELayer::Count)];

		std::vector<Entity>& operator[](ELayer L) { return Layers[static_cast<int>(L)]; }
		const std::vector<Entity>& operator[](ELayer L) const { return Layers[static_cast<int>(L)]; }
	};

	/**
	 * Holds the last known state and turns successive frames into deltas.
	 * Not thread safe: call from one thread (the game thread).
	 */
	class FrameDiffer
	{
	public:
		/** Entities that moved less than this (cm) are not resent. */
		double PositionEpsilon = 10.0;
		/** Degrees. */
		double YawEpsilon = 1.0;

		/**
		 * Applies a full frame. Returns a "delta" message, or an empty string when
		 * nothing changed. Every non-empty delta bumps Seq().
		 */
		std::string Update(const Frame& Next);

		/**
		 * Replaces the static rail layer. Encoded once here, not per Snapshot(),
		 * because a big save has thousands of track segments.
		 */
		void SetRails(const std::vector<Rail>& InRails);

		/** Full state as a "snapshot" message, tagged with the current Seq(). */
		std::string Snapshot() const;

		uint64_t Seq() const { return SeqCounter; }

	private:
		bool Changed(const Entity& A, const Entity& B) const;

		std::unordered_map<std::string, Entity> State[static_cast<int>(ELayer::Count)];
		std::string RailsJson = "[]";
		uint64_t SeqCounter = 0;
	};

	/** Minimal append-only JSON writer. Numbers are written as integers (cm, degrees). */
	class JsonWriter
	{
	public:
		void BeginObject();
		void EndObject();
		void BeginArray();
		void EndArray();
		void Key(const char* K);
		void String(const std::string& S);
		void Int(int64_t V);
		void Number(double V) { Int(Round(V)); }
		void Raw(const char* S);

		const std::string& Str() const { return Out; }
		std::string Take() { return std::move(Out); }

		static int64_t Round(double V);

	private:
		void Sep();

		std::string Out;
		std::vector<bool> First;
		bool AfterKey = false;
	};

	void WriteEntity(JsonWriter& W, const Entity& E);
}
