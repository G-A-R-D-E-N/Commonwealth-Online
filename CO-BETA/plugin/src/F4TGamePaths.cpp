#include "F4TGamePaths.h"

#include "F4TF4SECapabilities.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

#include <memory>

namespace F4T::GamePaths
{
	bool ResolveF4SEFolder(std::filesystem::path& a_out)
	{
		wchar_t* knownBuffer{ nullptr };
		const auto knownResult =
			REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, std::addressof(knownBuffer));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(knownBuffer, REX::W32::CoTaskMemFree);
		if (!knownPath || knownResult != 0) {
			return false;
		}

		std::filesystem::path path = knownPath.get();
		path /= "My Games";
		path /= F4T::F4SECapabilities::GetSaveFolderName();
		path /= "F4SE";
		a_out = path;
		return true;
	}
}
