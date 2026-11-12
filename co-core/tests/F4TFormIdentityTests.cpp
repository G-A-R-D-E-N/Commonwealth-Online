#include "F4TFormIdentity.h"

#include <cassert>

int main()
{
	using F4T::FormIdentity::IsValidRecordIdentity;
	using F4T::FormIdentity::Kind;
	using F4T::FormIdentity::SplitFormId;

	{
		const auto s = SplitFormId(0x0001F4A6u);
		assert(s.kind == Kind::Regular);
		assert(s.fileIndex == 0x00u);
		assert(s.localId == 0x0001F4A6u);
	}
	{
		const auto s = SplitFormId(0x0500ABCDu);
		assert(s.kind == Kind::Regular);
		assert(s.fileIndex == 0x05u);
		assert(s.localId == 0x00ABCDu);
	}
	{
		const auto s = SplitFormId(0x00FFFFFFu);
		assert(s.kind == Kind::Regular);
		assert(s.fileIndex == 0x00u);
		assert(s.localId == 0x00FFFFFFu);
	}
	{
		const auto s = SplitFormId(0xFE000000u);
		assert(s.kind == Kind::Light);
		assert(s.fileIndex == 0x000u);
		assert(s.localId == 0x000u);
	}
	{
		const auto s = SplitFormId(0xFE000FFFu);
		assert(s.kind == Kind::Light);
		assert(s.fileIndex == 0x000u);
		assert(s.localId == 0xFFFu);
	}
	{
		const auto s = SplitFormId(0xFE001000u);
		assert(s.kind == Kind::Light);
		assert(s.fileIndex == 0x001u);
		assert(s.localId == 0x000u);
	}
	{
		const auto s = SplitFormId(0xFEFFF000u);
		assert(s.kind == Kind::Light);
		assert(s.fileIndex == 0xFFFu);
		assert(s.localId == 0x000u);
	}
	{
		const auto s = SplitFormId(0xFEFFFFFFu);
		assert(s.kind == Kind::Light);
		assert(s.fileIndex == 0xFFFu);
		assert(s.localId == 0xFFFu);
	}
	{
		assert(SplitFormId(0xFF000000u).kind == Kind::Runtime);
		assert(SplitFormId(0xFF000800u).kind == Kind::Runtime);
		assert(SplitFormId(0xFFFFFFFFu).kind == Kind::Runtime);
	}

	assert(IsValidRecordIdentity({ "Fallout4.esm", 0x0001F4A6u }, Kind::Regular));
	assert(IsValidRecordIdentity({ "Fallout4.esm", 0x00FFFFFFu }, Kind::Regular));
	assert(!IsValidRecordIdentity({ "Fallout4.esm", 0x01000000u }, Kind::Regular));
	assert(!IsValidRecordIdentity({ "", 0x0001F4A6u }, Kind::Regular));
	assert(IsValidRecordIdentity({ "CommonwealthOnline.esp", 0x0FFFu }, Kind::Light));
	assert(!IsValidRecordIdentity({ "CommonwealthOnline.esp", 0x1000u }, Kind::Light));
	assert(!IsValidRecordIdentity({ "CommonwealthOnline.esp", 0x0001u }, Kind::Runtime));

	return 0;
}
