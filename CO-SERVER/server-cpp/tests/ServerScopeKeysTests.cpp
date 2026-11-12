#include "ServerScopeKeys.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace
{
	using CommonwealthOnline::Server::ScopeKeys::Canonical;
	using CommonwealthOnline::Server::ScopeKeys::FormKey;
	using CommonwealthOnline::Server::ScopeKeys::KeyOfWire;
	using CommonwealthOnline::Server::ScopeKeys::MovementScope;
	using CommonwealthOnline::Server::ScopeKeys::Plugins;
	using CommonwealthOnline::Server::ScopeKeys::Stamp;
	using CommonwealthOnline::Server::ScopeKeys::WireOf;
	using CommonwealthOnline::Server::ScopeKeys::Wires;
	using CommonwealthOnline::Server::SessionTypes::StoredMovementState;
	using F4T::LoadOrder::Plugin;
	using F4T::LoadOrder::PluginKind;
	using F4T::LoadOrder::PluginList;
	using F4T::NpcAuthority::Scope;

	Plugin Full(std::uint16_t a_index, std::string a_name)
	{
		return Plugin{ PluginKind::full, a_index, std::move(a_name) };
	}

	PluginList EarlyList()
	{
		PluginList list;
		list.full = { Full(0, "Fallout4.esm"), Full(1, "DLCRobot.esm") };
		return list;
	}

	PluginList LateList()
	{
		PluginList list;
		list.full = { Full(0, "Fallout4.esm"), Full(1, "DLCWorkshop01.esm"), Full(2, "DLCRobot.esm") };
		return list;
	}

	PluginList LightList()
	{
		PluginList list;
		list.full = { Full(0, "Fallout4.esm") };
		list.light = { Plugin{ PluginKind::light, 0, "Light.esl" } };
		return list;
	}

	Scope RawScope(const std::string& a_cellId, const std::string& a_worldspaceId)
	{
		return Scope{ a_cellId, a_worldspaceId };
	}

	void TestSameDlcCellAcrossLoadOrders()
	{
		const Plugins early = EarlyList();
		const Plugins late = LateList();
		assert(FormKey(early, "01000123") == "dlcrobot.esm:00000123");
		assert(FormKey(early, "01000123") == FormKey(late, "02000123"));
		const auto earlyScope = Canonical(early, RawScope("01000123", "01000ABC"));
		const auto lateScope = Canonical(late, RawScope("02000123", "02000ABC"));
		assert(earlyScope == lateScope);
		assert(earlyScope.cellId == "dlcrobot.esm:00000123");
		assert(earlyScope.worldspaceId == "dlcrobot.esm:00000ABC");
	}

	void TestRawFallback()
	{
		const Plugins none = std::nullopt;
		const Plugins empty = PluginList{};
		const Plugins early = EarlyList();
		assert(FormKey(none, "01000123") == "01000123");
		assert(FormKey(empty, "01000123") == "01000123");
		assert(FormKey(early, "05000123") == "05000123");
		assert(FormKey(early, "not-hex") == "not-hex");
		assert(FormKey(early, "").empty());
		const auto raw = Canonical(none, RawScope("01000123", ""));
		assert(raw.cellId == "01000123");
		assert(raw.worldspaceId.empty());
	}

	void TestListAndNoListBaseGameCell()
	{
		const Plugins none = std::nullopt;
		const Plugins early = EarlyList();
		assert(FormKey(early, "0000003C") == "fallout4.esm:0000003C");
		assert(FormKey(none, "0000003C") == FormKey(early, "0000003C"));
		assert(FormKey(none, "0000003c") == "fallout4.esm:0000003C");
		const auto noListScope = Canonical(none, RawScope("0000003C", "0000000A"));
		assert(noListScope == Canonical(early, RawScope("0000003C", "0000000A")));
		assert(noListScope.worldspaceId == "fallout4.esm:0000000A");
		assert(FormKey(none, "01000123") == "01000123");
		assert(FormKey(none, "00000000") == "00000000");
		assert(FormKey(none, "FE000ABC") == "FE000ABC");
		assert(FormKey(LightList(), "FE000ABC") == "light.esl:00000ABC");
		assert(FormKey(LightList(), "0000003C") == "fallout4.esm:0000003C");
	}

	void TestStampAndMovementScope()
	{
		const Plugins early = EarlyList();
		const Plugins late = LateList();

		StoredMovementState unstamped;
		unstamped.cellId = "02000123";
		assert(MovementScope(unstamped).cellId == "02000123");

		StoredMovementState moved;
		moved.cellId = "01000123";
		Stamp(moved, early);
		assert(moved.cellKey == "dlcrobot.esm:00000123");
		assert(moved.worldspaceKey.empty());
		assert(MovementScope(moved).cellId == "dlcrobot.esm:00000123");

		StoredMovementState next = moved;
		moved.cellKey = "kept";
		Stamp(next, early, moved);
		assert(next.cellKey == "kept");
		next.cellId = "02000123";
		Stamp(next, late, moved);
		assert(next.cellKey == "dlcrobot.esm:00000123");
	}

	void TestWireLookups()
	{
		Wires wires;
		const auto key = RawScope("dlcrobot.esm:00000123", "");
		const auto wire = RawScope("01000123", "");
		const auto other = RawScope("dlcrobot.esm:00000456", "");
		wires[key] = wire;
		assert(WireOf(wires, key) == wire);
		assert(WireOf(wires, other) == other);
		assert(KeyOfWire(wires, wire) == key);
		assert(!KeyOfWire(wires, other).has_value());
	}
}

int main()
{
	TestSameDlcCellAcrossLoadOrders();
	TestRawFallback();
	TestListAndNoListBaseGameCell();
	TestStampAndMovementScope();
	TestWireLookups();
	return 0;
}
