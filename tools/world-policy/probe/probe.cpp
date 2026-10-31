#include "fo4/fo4.hpp"
#include "fo4/schema/vmad.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using fo4::Signature;
using fo4::Subrecord;

std::string jesc(std::string_view text) {
  std::string out;
  for (const unsigned char ch : text) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
          out += buf;
        } else {
          out += static_cast<char>(ch);
        }
        break;
    }
  }
  return out;
}

std::string jstr(std::string_view text) { return "\"" + jesc(text) + "\""; }

std::string hexs(std::span<const std::byte> data) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 2);
  for (const auto byte : data) {
    const auto value = std::to_integer<unsigned>(byte);
    out.push_back(digits[value >> 4]);
    out.push_back(digits[value & 0xF]);
  }
  return out;
}

std::string hexu(std::uint32_t value) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%08X", value);
  return buf;
}

std::uint8_t u8(std::span<const std::byte> data, std::size_t offset) {
  if (data.size() < offset + 1) return 0;
  return std::to_integer<std::uint8_t>(data[offset]);
}

std::uint16_t u16(std::span<const std::byte> data, std::size_t offset) {
  if (data.size() < offset + 2) return 0;
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[offset]) |
                                    (std::to_integer<std::uint8_t>(data[offset + 1]) << 8));
}

std::uint32_t u32(std::span<const std::byte> data, std::size_t offset) {
  if (data.size() < offset + 4) return 0;
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset]) |
                                    (std::to_integer<std::uint8_t>(data[offset + 1]) << 8) |
                                    (std::to_integer<std::uint8_t>(data[offset + 2]) << 16) |
                                    (std::to_integer<std::uint8_t>(data[offset + 3]) << 24));
}

const Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  const auto wanted = Signature::from_string(signature);
  for (const auto& sub : record.subrecords) {
    if (sub.signature == wanted) return &sub;
  }
  return nullptr;
}

std::string zstring(std::span<const std::byte> data) {
  std::string value(reinterpret_cast<const char*>(data.data()), data.size());
  while (!value.empty() && value.back() == '\0') value.pop_back();
  return value;
}

std::string edid_of(const fo4::Record& record) {
  const auto* sub = find_sub(record, "EDID");
  if (sub == nullptr) return {};
  return zstring(sub->data);
}

struct Info {
  Signature signature{};
  std::string edid;
  bool has_full{false};
  std::uint32_t full_id{0};
  bool fact_vendor_flag{false};
  bool fact_has_vend{false};
};

std::string form_key_text(const fo4::PluginFormIdContext& ctx, std::uint32_t raw) {
  if (raw == 0) return {};
  try {
    return fo4::resolve_plugin_form_id(fo4::RawFormId{raw}, ctx).to_string();
  } catch (...) {
    return "<unresolved>";
  }
}

std::string subs_json(const fo4::Record& record, std::span<const std::string_view> only) {
  std::string out = "[";
  bool first = true;
  for (const auto& sub : record.subrecords) {
    const auto text = sub.signature.to_string();
    if (!only.empty()) {
      bool wanted = false;
      for (const auto name : only) {
        if (text == name) wanted = true;
      }
      if (!wanted) continue;
    }
    if (!first) out += ",";
    first = false;
    out += "[" + jstr(text) + "," + jstr(hexs(sub.data)) + "]";
  }
  out += "]";
  return out;
}

std::string keyword_list(const fo4::Record& record, const fo4::PluginFormIdContext& ctx) {
  const auto* kwda = find_sub(record, "KWDA");
  std::string out = "[";
  if (kwda != nullptr) {
    bool first = true;
    for (std::size_t offset = 0; offset + 4 <= kwda->data.size(); offset += 4) {
      if (!first) out += ",";
      first = false;
      out += jstr(form_key_text(ctx, u32(kwda->data, offset)));
    }
  }
  out += "]";
  return out;
}

std::string faction_list(const fo4::Record& record,
                         const fo4::PluginFormIdContext& ctx,
                         const std::unordered_map<std::uint32_t, Info>& index) {
  std::string out = "[";
  bool first = true;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != Signature::from_string("SNAM")) continue;
    if (sub.data.size() < 5) continue;
    const auto raw = u32(sub.data, 0);
    if (!first) out += ",";
    first = false;
    const auto found = index.find(raw);
    out += "{\"faction\":" + jstr(form_key_text(ctx, raw)) +
           ",\"rank\":" + std::to_string(static_cast<int>(static_cast<std::int8_t>(u8(sub.data, 4)))) +
           ",\"edid\":" + jstr(found == index.end() ? std::string{} : found->second.edid) +
           ",\"vendor\":" + ((found != index.end() && (found->second.fact_vendor_flag || found->second.fact_has_vend)) ? "true" : "false") + "}";
  }
  out += "]";
  return out;
}

std::string template_list(const fo4::Record& record,
                          const fo4::PluginFormIdContext& ctx,
                          const std::unordered_map<std::uint32_t, Info>& index) {
  std::string out = "[";
  const auto* tpta = find_sub(record, "TPTA");
  if (tpta != nullptr) {
    bool first = true;
    for (std::size_t offset = 0; offset + 4 <= tpta->data.size(); offset += 4) {
      const auto raw = u32(tpta->data, offset);
      if (raw == 0) continue;
      if (!first) out += ",";
      first = false;
      const auto found = index.find(raw);
      out += "{\"form\":" + jstr(form_key_text(ctx, raw)) +
             ",\"edid\":" + jstr(found == index.end() ? std::string{} : found->second.edid) +
             ",\"slot\":" + std::to_string(offset / 4) + "}";
    }
  }
  out += "]";
  return out;
}

std::string log_text_json(const std::optional<fo4::schema::LocalizedString>& text) {
  if (!text.has_value()) return "null";
  if (text->id.has_value()) return std::to_string(*text->id);
  return jstr(text->inline_text());
}

void write_quest(std::ostream& out,
                 const fo4::Plugin& plugin,
                 const fo4::Record& record,
                 const fo4::PluginFormIdContext& ctx) {
  const auto key = record.form_key;
  out << "{\"sig\":\"QUST\",\"id\":" << jstr(hexu(record.header.form_id.value()))
      << ",\"fk\":" << jstr(key.to_string())
      << ",\"flags\":" << jstr(hexu(record.header.flags.value()))
      << ",\"edid\":" << jstr(edid_of(record));

  const auto* full = find_sub(record, "FULL");
  out << ",\"full\":";
  if (full != nullptr && full->data.size() == 4) {
    out << u32(full->data, 0);
  } else if (full != nullptr) {
    out << jstr(zstring(full->data));
  } else {
    out << "null";
  }

  const auto* dnam = find_sub(record, "DNAM");
  out << ",\"dnam_hex\":" << jstr(dnam == nullptr ? std::string{} : hexs(dnam->data));
  out << ",\"quest_flags\":" << (dnam == nullptr ? 0u : u16(dnam->data, 0));
  out << ",\"quest_type\":" << (dnam == nullptr ? 0u : u32(dnam->data, 8));
  out << ",\"subs\":" << subs_json(record, std::vector<std::string_view>{"EDID", "FULL", "DNAM", "ENAM", "LNAM", "XNAM", "ANAM", "NNAM", "GNAM", "SNAM", "VMAD", "QTGL", "FLTR"});

  std::string stages = "[";
  try {
    const auto decoded = fo4::forms::decode_quest_stages(plugin, key);
    bool first = true;
    for (const auto& stage : decoded.stages) {
      if (!first) stages += ",";
      first = false;
      stages += "{\"i\":" + std::to_string(stage.index) + ",\"flags\":" + std::to_string(stage.flags) +
                ",\"logs\":[";
      bool first_log = true;
      for (const auto& log : stage.log_entries) {
        if (!first_log) stages += ",";
        first_log = false;
        stages += "{\"flags\":" + std::to_string(log.flags) +
                  ",\"text\":" + log_text_json(log.log_text) +
                  ",\"note\":" + (log.note.has_value() ? jstr(*log.note) : std::string{"null"}) + "}";
      }
      stages += "]}";
    }
  } catch (const std::exception& error) {
    stages += jstr(std::string{"error: "} + error.what());
  }
  stages += "]";
  out << ",\"stages\":" << stages;

  std::string objectives = "[";
  try {
    const auto decoded = fo4::forms::decode_quest_objectives(plugin, key);
    bool first = true;
    for (const auto& objective : decoded.objectives) {
      if (!first) objectives += ",";
      first = false;
      objectives += "{\"i\":" + std::to_string(objective.index) + ",\"flags\":" + std::to_string(objective.flags) +
                    ",\"text\":" + log_text_json(objective.display_text) + ",\"targets\":[";
      bool first_target = true;
      for (const auto& target : objective.targets) {
        if (!first_target) objectives += ",";
        first_target = false;
        objectives += "{\"alias\":" + std::to_string(target.alias) + ",\"flags\":" + std::to_string(target.flags) + "}";
      }
      objectives += "]}";
    }
  } catch (const std::exception& error) {
    objectives += jstr(std::string{"error: "} + error.what());
  }
  objectives += "]";
  out << ",\"objectives\":" << objectives;

  std::string aliases = "[";
  try {
    const auto decoded = fo4::forms::decode_quest_aliases(plugin, key);
    out << ",\"next_alias_id\":" << decoded.next_alias_id;
    bool first = true;
    for (const auto& alias : decoded.aliases) {
      if (!first) aliases += ",";
      first = false;
      const char* kind = alias.kind == fo4::forms::QuestAliasKind::reference ? "reference"
                         : alias.kind == fo4::forms::QuestAliasKind::location ? "location"
                                                                             : "collection";
      aliases += "{\"kind\":" + jstr(kind) + ",\"id\":" + std::to_string(alias.id) +
                 ",\"name\":" + (alias.name.has_value() ? jstr(*alias.name) : std::string{"null"}) +
                 ",\"flags\":" + (alias.flags.has_value() ? std::to_string(*alias.flags) : std::string{"null"}) +
                 ",\"fill\":" + std::to_string(alias.fill.index()) +
                 ",\"factions\":" + std::to_string(alias.factions.size()) +
                 ",\"items\":" + std::to_string(alias.items.size()) +
                 ",\"unresolved\":" + std::to_string(alias.unresolved_links.size()) + "}";
    }
  } catch (const std::exception& error) {
    aliases += jstr(std::string{"error: "} + error.what());
  }
  aliases += "]";
  out << ",\"aliases\":" << aliases;

  const auto* vmad = find_sub(record, "VMAD");
  out << ",\"vmad\":";
  if (vmad == nullptr) {
    out << "null";
  } else {
    const auto layout = fo4::inspect_quest_vmad(vmad->data);
    out << "{\"unparseable\":" << (layout.unparseable ? "true" : "false")
        << ",\"unsupported\":" << (layout.unsupported ? "true" : "false")
        << ",\"object_format\":" << layout.object_format
        << ",\"fragments\":[";
    bool first = true;
    for (const auto& fragment : layout.fragments) {
      if (!first) out << ",";
      first = false;
      out << "{\"stage\":" << fragment.stage << ",\"stage_index\":" << fragment.stage_index << "}";
    }
    out << "],\"aliases\":[";
    first = true;
    for (const auto& alias : layout.aliases) {
      if (!first) out << ",";
      first = false;
      out << "{\"alias\":" << alias.alias_id << "}";
    }
    out << "]}";
  }

  out << "}\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: probe Fallout4.esm outdir | probe archive.ba2 list <substring>\n";
    return 2;
  }
  const std::filesystem::path first = argv[1];
  if (first.extension() == ".ba2") {
    const std::string needle = argc > 3 ? argv[3] : std::string{};
    const auto archive = fo4::archive::read_ba2_general_file(first);
    std::size_t shown = 0;
    for (const auto& entry : archive.entries) {
      if (!needle.empty() && entry.path.find(needle) == std::string::npos) continue;
      if (shown++ < 40) std::cout << entry.path << "\n";
    }
    std::cout << "entries " << archive.entries.size() << " matched " << shown << "\n";
    return 0;
  }
  const std::filesystem::path esm_path = argv[1];
  const std::filesystem::path out_dir = argv[2];
  std::filesystem::create_directories(out_dir);

  const auto plugin = fo4::read_plugin_file(esm_path);
  const auto ctx = fo4::plugin_form_id_context(plugin);
  const auto records = fo4::collect_records(plugin);

  std::unordered_map<std::uint32_t, Info> index;
  std::unordered_set<std::uint32_t> door_ids;
  for (const auto* record : records) {
    Info info;
    info.signature = record->header.signature;
    info.edid = edid_of(*record);
    const auto* full = find_sub(*record, "FULL");
    if (full != nullptr && full->data.size() == 4) {
      info.has_full = true;
      info.full_id = u32(full->data, 0);
    }
    if (record->header.signature == Signature::from_string("FACT")) {
      const auto* data = find_sub(*record, "DATA");
      info.fact_vendor_flag = data != nullptr && (u32(data->data, 0) & 0x4000u) != 0;
      info.fact_has_vend = find_sub(*record, "VEND") != nullptr;
    }
    if (record->header.signature == Signature::from_string("DOOR")) {
      door_ids.insert(record->header.form_id.value());
    }
    index.emplace(record->header.form_id.value(), info);
  }

  std::ofstream npc_out(out_dir / "npc.jsonl");
  std::ofstream qust_out(out_dir / "qust.jsonl");
  std::ofstream door_out(out_dir / "door.jsonl");
  std::ofstream refdoor_out(out_dir / "refdoor.jsonl");
  std::ofstream fact_out(out_dir / "fact.jsonl");
  std::ofstream race_out(out_dir / "race.jsonl");
  std::ofstream kwd_out(out_dir / "kwd.jsonl");

  std::uint64_t npc_count = 0;
  std::uint64_t qust_count = 0;
  std::uint64_t door_count = 0;
  std::uint64_t refdoor_count = 0;

  for (const auto* record : records) {
    const auto signature = record->header.signature;
    if (signature == Signature::from_string("NPC_")) {
      ++npc_count;
      const auto* race = find_sub(*record, "RNAM");
      const auto* tplt = find_sub(*record, "TPLT");
      const auto* acbs = find_sub(*record, "ACBS");
      npc_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
              << ",\"fk\":" << jstr(record->form_key.to_string())
              << ",\"flags\":" << jstr(hexu(record->header.flags.value()))
              << ",\"edid\":" << jstr(edid_of(*record))
              << ",\"acbs\":" << (acbs == nullptr ? 0u : u32(acbs->data, 0))
              << ",\"acbs_size\":" << (acbs == nullptr ? 0u : static_cast<unsigned>(acbs->data.size()))
              << ",\"template_flags\":" << (acbs == nullptr ? 0u : u16(acbs->data, 16))
              << ",\"race\":" << jstr(race == nullptr ? std::string{} : form_key_text(ctx, u32(race->data, 0)))
              << ",\"race_edid\":" << jstr([&] {
                   const auto found = race == nullptr ? index.end() : index.find(u32(race->data, 0));
                   return found == index.end() ? std::string{} : found->second.edid;
                 }())
              << ",\"tplt\":" << jstr(tplt == nullptr ? std::string{} : form_key_text(ctx, u32(tplt->data, 0)))
              << ",\"aggression\":" << [&] {
                   const auto* aidt = find_sub(*record, "AIDT");
                   return aidt == nullptr ? -1 : static_cast<int>(u8(aidt->data, 0));
                 }()
              << ",\"aidt\":" << jstr([&] {
                   const auto* aidt = find_sub(*record, "AIDT");
                   return aidt == nullptr ? std::string{} : hexs(aidt->data);
                 }())
              << ",\"factions\":" << faction_list(*record, ctx, index)
              << ",\"template\":" << template_list(*record, ctx, index)
              << ",\"keywords\":" << keyword_list(*record, ctx)
              << ",\"subs\":" << subs_json(*record, std::vector<std::string_view>{"EDID", "FULL", "ACBS", "RNAM", "SNAM", "TPTA", "TPLT", "KWDA", "KSIZ", "VTCK", "CNAM", "INAM"})
              << "}\n";
    } else if (signature == Signature::from_string("QUST")) {
      ++qust_count;
      write_quest(qust_out, plugin, *record, ctx);
    } else if (signature == Signature::from_string("DOOR")) {
      ++door_count;
      door_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
               << ",\"fk\":" << jstr(record->form_key.to_string())
               << ",\"flags\":" << jstr(hexu(record->header.flags.value()))
               << ",\"edid\":" << jstr(edid_of(*record))
               << ",\"keywords\":" << keyword_list(*record, ctx)
               << ",\"subs\":" << subs_json(*record, {}) << "}\n";
    } else if (signature == Signature::from_string("FACT")) {
      const auto* data = find_sub(*record, "DATA");
      fact_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
               << ",\"fk\":" << jstr(record->form_key.to_string())
               << ",\"edid\":" << jstr(edid_of(*record))
               << ",\"flags\":" << (data == nullptr ? 0u : u32(data->data, 0))
               << ",\"has_vend\":" << (find_sub(*record, "VEND") != nullptr ? "true" : "false")
               << ",\"has_venv\":" << (find_sub(*record, "VENV") != nullptr ? "true" : "false")
               << ",\"subs\":" << subs_json(*record, std::vector<std::string_view>{"EDID", "FULL", "DATA", "VEND", "VENV", "XNAM"})
               << "}\n";
    } else if (signature == Signature::from_string("RACE")) {
      race_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
               << ",\"fk\":" << jstr(record->form_key.to_string())
               << ",\"edid\":" << jstr(edid_of(*record))
               << ",\"subs\":" << subs_json(*record, std::vector<std::string_view>{"EDID", "FULL", "DATA", "KWDA", "KSIZ"})
               << "}\n";
    } else if (signature == Signature::from_string("KYWD")) {
      kwd_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
              << ",\"fk\":" << jstr(record->form_key.to_string())
              << ",\"edid\":" << jstr(edid_of(*record)) << "}\n";
    } else if (signature == Signature::from_string("REFR") || signature == Signature::from_string("ACHR")) {
      const auto* name = find_sub(*record, "NAME");
      if (name == nullptr || name->data.size() < 4) continue;
      const auto base = u32(name->data, 0);
      if (door_ids.find(base) == door_ids.end()) continue;
      ++refdoor_count;
      const auto* xloc = find_sub(*record, "XLOC");
      const auto* xtel = find_sub(*record, "XTEL");
      refdoor_out << "{\"id\":" << jstr(hexu(record->header.form_id.value()))
                  << ",\"fk\":" << jstr(record->form_key.to_string())
                  << ",\"sig\":" << jstr(signature.to_string())
                  << ",\"flags\":" << jstr(hexu(record->header.flags.value()))
                  << ",\"edid\":" << jstr(edid_of(*record))
                  << ",\"base\":" << jstr(form_key_text(ctx, base))
                  << ",\"base_edid\":" << jstr(index.count(base) != 0 ? index.at(base).edid : std::string{})
                  << ",\"lock\":";
      if (xloc == nullptr) {
        refdoor_out << "null";
      } else {
        refdoor_out << "{\"level\":" << static_cast<unsigned>(u8(xloc->data, 0))
                    << ",\"key\":" << jstr(form_key_text(ctx, u32(xloc->data, 4)))
                    << ",\"flags\":" << static_cast<unsigned>(u8(xloc->data, 8))
                    << ",\"size\":" << xloc->data.size() << "}";
      }
      refdoor_out << ",\"teleport\":";
      if (xtel == nullptr) {
        refdoor_out << "null";
      } else {
        refdoor_out << "{\"door\":" << jstr(form_key_text(ctx, u32(xtel->data, 0)))
                    << ",\"flags\":" << u32(xtel->data, 28)
                    << ",\"interior\":" << jstr(form_key_text(ctx, u32(xtel->data, 32)))
                    << ",\"size\":" << xtel->data.size() << "}";
      }
      refdoor_out << ",\"subs\":" << subs_json(*record, std::vector<std::string_view>{"EDID", "NAME", "XLOC", "XTEL", "XESP", "DATA", "XSCL"})
                  << "}\n";
    }
  }

  std::uint64_t string_entries = 0;
  const auto ba2_path = esm_path.parent_path() / "Fallout4 - Misc.ba2";
  if (std::filesystem::exists(ba2_path)) {
    const auto archive = fo4::archive::read_ba2_general_file(ba2_path);
    struct Wanted {
      std::string needle;
      fo4::localization::StringTableKind kind;
      const char* out_name;
    };
    const Wanted wanted[] = {
        {"fallout4_english.strings", fo4::localization::StringTableKind::strings, "strings.tsv"},
        {"fallout4_english.dlstrings", fo4::localization::StringTableKind::dlstrings, "dlstrings.tsv"},
        {"fallout4_english.ilstrings", fo4::localization::StringTableKind::ilstrings, "ilstrings.tsv"},
    };
    for (const auto& want : wanted) {
      for (const auto& entry : archive.entries) {
        std::string lowered = entry.path;
        for (auto& ch : lowered) {
          if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        if (lowered.find(want.needle) == std::string::npos) continue;
        const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
        const auto table = fo4::localization::read_string_table(bytes, want.kind);
        std::ofstream out(out_dir / want.out_name, std::ios::binary);
        for (const auto& item : table.entries) {
          out << item.id << '\t';
          out.write(reinterpret_cast<const char*>(item.text.data()), static_cast<std::streamsize>(item.text.size()));
          out << '\n';
        }
        string_entries += table.entries.size();
        std::cout << "string_table " << entry.path << " entries " << table.entries.size() << "\n";
      }
    }
  } else {
    std::cout << "string_table missing: " << ba2_path.string() << "\n";
  }

  std::cout << "{\"npc\":" << npc_count << ",\"qust\":" << qust_count << ",\"door\":" << door_count
            << ",\"placed_door_refs\":" << refdoor_count << ",\"string_entries\":" << string_entries << "}\n";
  return 0;
}
