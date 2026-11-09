#include "F4TVendorTradeEscrow.h"

#include <limits>
#include <stdexcept>

namespace F4T::VendorTradeEscrow
{
	namespace
	{
		using Json = nlohmann::json;

		std::uint32_t ReadUnsigned(const Json& a_json, const char* a_key)
		{
			const auto& value = a_json.at(a_key);
			if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
				throw std::out_of_range(a_key);
			}
			return value.get<std::uint32_t>();
		}

		Json EncodeContext(const Context& a_context)
		{
			return Json{ { "serverKey", a_context.serverKey }, { "worldId", a_context.worldId }, { "profileId", a_context.profileId }, { "characterSlot", a_context.characterSlot ? Json(*a_context.characterSlot) : Json(nullptr) } };
		}

		Context DecodeContext(const Json& a_json)
		{
			Context context;
			context.serverKey = a_json.at("serverKey").get<std::string>();
			context.worldId = a_json.at("worldId").get<std::string>();
			context.profileId = a_json.at("profileId").get<std::string>();
			if (!a_json.at("characterSlot").is_null()) {
				context.characterSlot = ReadUnsigned(a_json, "characterSlot");
			}
			return context;
		}

		Json EncodeEscrow(const Escrow& a_escrow)
		{
			return Json{ { "operationId", a_escrow.operationId }, { "taken", VT::StockLinesToJson(a_escrow.taken) }, { "capsTaken", a_escrow.capsTaken }, { "grant", VT::StockLinesToJson(a_escrow.grant) }, { "capsGrant", a_escrow.capsGrant }, { "packet", a_escrow.packet }, { "context", EncodeContext(a_escrow.context) } };
		}

		Escrow DecodeEscrow(const Json& a_json)
		{
			Escrow escrow;
			escrow.operationId = a_json.at("operationId").get<std::string>();
			if (escrow.operationId.empty() || escrow.operationId.size() > VT::MaximumOperationIdLength) {
				throw std::invalid_argument("operationId");
			}
			if (!VT::ParseStockLines(Json{ { "items", a_json.at("taken") } }, escrow.taken)) {
				throw std::invalid_argument("taken");
			}
			escrow.capsTaken = ReadUnsigned(a_json, "capsTaken");
			if (escrow.capsTaken > VT::MaximumCaps) {
				throw std::invalid_argument("capsTaken");
			}
			if (!VT::ParseStockLines(Json{ { "items", a_json.at("grant") } }, escrow.grant)) {
				throw std::invalid_argument("grant");
			}
			escrow.capsGrant = ReadUnsigned(a_json, "capsGrant");
			if (escrow.capsGrant > VT::MaximumCaps) {
				throw std::invalid_argument("capsGrant");
			}
			escrow.packet = a_json.at("packet");
			if (!escrow.packet.is_object() || escrow.packet.value("operationId", std::string{}) != escrow.operationId) {
				throw std::invalid_argument("packet");
			}
			escrow.context = DecodeContext(a_json.at("context"));
			return escrow;
		}

		Json EncodePayout(const Payout& a_payout)
		{
			return Json{ { "escrow", EncodeEscrow(a_payout.escrow) }, { "grant", a_payout.grant } };
		}

		Payout DecodePayout(const Json& a_json)
		{
			Payout payout;
			payout.escrow = DecodeEscrow(a_json.at("escrow"));
			payout.grant = a_json.at("grant").get<bool>();
			return payout;
		}
	}

	bool MayReconcile(const Context& a_recorded, const Context& a_current)
	{
		return !a_current.serverKey.empty() && a_recorded == a_current;
	}

	std::vector<std::pair<std::string, nlohmann::json>> DueResends(std::map<std::string, Escrow>& a_unresolved, const Context& a_current, bool a_reconnected, Clock::time_point a_now)
	{
		std::vector<std::pair<std::string, nlohmann::json>> due;
		for (auto& [operationId, escrow] : a_unresolved) {
			if (!MayReconcile(escrow.context, a_current)) {
				escrow.nextResend.reset();
				continue;
			}
			if (a_reconnected || !escrow.nextResend) {
				escrow.nextResend = a_now;
			}
			if (a_now >= *escrow.nextResend) {
				escrow.packet["resend"] = true;
				escrow.nextResend = a_now + kResendDelay;
				due.emplace_back(operationId, escrow.packet);
			}
		}
		return due;
	}

	std::optional<Escrow> TakeResolved(std::map<std::string, Escrow>& a_unresolved, const std::string& a_operationId, const Context& a_current)
	{
		const auto found = a_unresolved.find(a_operationId);
		if (found == a_unresolved.end() || !MayReconcile(found->second.context, a_current)) {
			return std::nullopt;
		}
		Escrow escrow = std::move(found->second);
		a_unresolved.erase(found);
		return escrow;
	}

	std::vector<Payout> TakeDuePayouts(std::vector<Payout>& a_pending, const Context& a_current)
	{
		std::vector<Payout> due;
		std::vector<Payout> parked;
		for (auto& payout : a_pending) {
			(MayReconcile(payout.escrow.context, a_current) ? due : parked).push_back(std::move(payout));
		}
		a_pending = std::move(parked);
		return due;
	}

	std::size_t CountParkedFor(const std::map<std::string, Escrow>& a_unresolved, const std::vector<Payout>& a_payouts, const Context& a_current)
	{
		std::size_t count = 0;
		for (const auto& entry : a_unresolved) {
			if (entry.second.context == a_current) {
				++count;
			}
		}
		for (const auto& payout : a_payouts) {
			if (payout.escrow.context == a_current) {
				++count;
			}
		}
		return count;
	}

	std::optional<std::string> EncodeParked(const Parked& a_parked)
	{
		if (a_parked.unresolved.size() + a_parked.payouts.size() > kMaximumParkedEntries) {
			return std::nullopt;
		}
		try {
			Json unresolved = Json::array();
			for (const auto& entry : a_parked.unresolved) {
				unresolved.push_back(EncodeEscrow(entry.second));
			}
			Json payouts = Json::array();
			for (const auto& payout : a_parked.payouts) {
				payouts.push_back(EncodePayout(payout));
			}
			std::string text = Json{ { "unresolved", unresolved }, { "payouts", payouts } }.dump();
			if (text.size() > kMaximumParkedBytes) {
				return std::nullopt;
			}
			return text;
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	bool FitsParked(const Parked& a_parked, const Escrow& a_escrow)
	{
		// Constraint: every entry is measured as its payout form (grant false), the largest one, because an unresolved escrow becomes a payout later.
		Parked worst;
		for (const auto& entry : a_parked.unresolved) {
			worst.payouts.push_back(Payout{ entry.second, false });
		}
		for (const auto& payout : a_parked.payouts) {
			worst.payouts.push_back(Payout{ payout.escrow, false });
		}
		worst.payouts.push_back(Payout{ a_escrow, false });
		return EncodeParked(worst).has_value();
	}

	std::optional<Parked> DecodeParked(const std::string& a_text)
	{
		if (a_text.size() > kMaximumParkedBytes) {
			return std::nullopt;
		}
		try {
			const auto root = Json::parse(a_text);
			const auto& unresolved = root.at("unresolved");
			const auto& payouts = root.at("payouts");
			if (!unresolved.is_array() || !payouts.is_array() || unresolved.size() + payouts.size() > kMaximumParkedEntries) {
				return std::nullopt;
			}
			Parked parked;
			for (const auto& item : unresolved) {
				Escrow escrow = DecodeEscrow(item);
				auto key = escrow.operationId;
				if (!parked.unresolved.emplace(std::move(key), std::move(escrow)).second) {
					return std::nullopt;
				}
			}
			for (const auto& item : payouts) {
				parked.payouts.push_back(DecodePayout(item));
			}
			return parked;
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}
}
