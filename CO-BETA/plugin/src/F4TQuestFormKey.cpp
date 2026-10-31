#include "pch.h"

#include "F4TQuestFormKey.h"

#include "RE/T/TESDataHandler.h"
#include "RE/T/TESFile.h"

namespace F4T::QuestFormKey
{
	std::size_t GetLoadOrderStamp()
	{
		const auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return 0;
		}
		return (data->compiledFileCollection.files.size() << 16) ^ data->compiledFileCollection.smallFiles.size();
	}

	LoadOrder GetLoadOrder()
	{
		LoadOrder loadOrder;
		const auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return loadOrder;
		}

		for (const auto* file : data->compiledFileCollection.files) {
			if (file) {
				loadOrder.push_back({ std::string(file->GetFilename()), QuestKey::FileKind::Regular, file->GetCompileIndex() });
			}
		}
		for (const auto* file : data->compiledFileCollection.smallFiles) {
			if (file) {
				loadOrder.push_back({ std::string(file->GetFilename()), QuestKey::FileKind::Light, file->GetSmallFileCompileIndex() });
			}
		}
		return loadOrder;
	}

	std::optional<std::string> ToKey(std::uint32_t a_formId, const LoadOrder& a_loadOrder)
	{
		const auto key = QuestKey::FromFormId(a_formId, a_loadOrder);
		if (!key) {
			return std::nullopt;
		}
		return QuestKey::Format(*key);
	}

	std::optional<std::uint32_t> ToFormId(std::string_view a_key, const LoadOrder& a_loadOrder)
	{
		const auto key = QuestKey::Parse(a_key);
		if (!key) {
			return std::nullopt;
		}
		return QuestKey::ToFormId(*key, a_loadOrder);
	}
}
