#include "F4TProfanityFilter.h"

#include <cassert>

namespace
{
	using F4T::ProfanityFilter::ContainsBlockedWord;
	using F4T::ProfanityFilter::LoadWords;
	using F4T::ProfanityFilter::MaskBlockedWords;
	using F4T::ProfanityFilter::ProfanityForced;
	using F4T::ProfanityFilter::Scope;
	using F4T::ProfanityFilter::SetProfanityEnabled;

	constexpr const char* kFixture =
		"# fixture\n[slurs]\n*nigger\n*nigga\ndyke\n+retard\nspic\nnig nog\n[profanity]\n+fuck\n+shit\n+slut\n*motherfuck\nass\nblow job\nescort\nsnatch\n";
}

int main()
{
	assert(!LoadWords("[profanity]\nshit\n"));
	assert(LoadWords(kFixture));

	assert(!ContainsBlockedWord("Hello there, wasteland traveler!"));
	assert(ContainsBlockedWord("you nigger"));
	assert(ContainsBlockedWord("Ni99er"));
	assert(ContainsBlockedWord("n1gg3r"));
	assert(ContainsBlockedWord("n i g g e r"));
	assert(ContainsBlockedWord("nigga!you"));
	assert(ContainsBlockedWord("what a dyke"));
	assert(ContainsBlockedWord("retarded"));
	assert(ContainsBlockedWord("a nig nog b"));
	assert(!ContainsBlockedWord("Hannigan and Nigeria"));
	assert(!ContainsBlockedWord("retardant, Dykestra"));
	assert(MaskBlockedWords("you are a nigger") == "you are a ******");
	assert(MaskBlockedWords("nigga!you") == "*********");
	assert(MaskBlockedWords("clean text") == "clean text");

	assert(!ContainsBlockedWord("what the fuck, shit, f u c k, fuck!you"));
	assert(!ContainsBlockedWord("a blow job"));

	SetProfanityEnabled(true);
	assert(ContainsBlockedWord("what the fuck"));
	assert(ContainsBlockedWord("fuuuck"));
	assert(ContainsBlockedWord("fucked"));
	assert(ContainsBlockedWord("shits"));
	assert(ContainsBlockedWord("sh1t"));
	assert(ContainsBlockedWord("$hit!"));
	assert(ContainsBlockedWord("fuck!you"));
	assert(ContainsBlockedWord("f u c k"));
	assert(ContainsBlockedWord("f.u.c.k"));
	assert(ContainsBlockedWord("$lut"));
	assert(ContainsBlockedWord("a Blow-Job b"));
	assert(!ContainsBlockedWord("blow the job"));
	assert(!ContainsBlockedWord("Scunthorpe class assassin, Dickinson, cocktail, escorted, snatched, I cocked the shotgun"));
	assert(MaskBlockedWords("you are a shit player") == "you are a **** player");
	assert(MaskBlockedWords("fuck!you") == "****!you");
	assert(MaskBlockedWords("$hit!") == "****!");
	SetProfanityEnabled(false);
	assert(!ContainsBlockedWord("fuck"));

	assert(!ProfanityForced());
	assert(!ContainsBlockedWord("what the fuck", Scope::Slurs));
	assert(ContainsBlockedWord("what the fuck", Scope::All));
	assert(ContainsBlockedWord("you nigger", Scope::Slurs));
	assert(MaskBlockedWords("fuck you nigger", Scope::Slurs) == "fuck you ******");
	assert(MaskBlockedWords("fuck you nigger", Scope::All) == "**** you ******");
	SetProfanityEnabled(true);
	assert(ProfanityForced());
	assert(MaskBlockedWords("fuck you") == "**** you");
	assert(MaskBlockedWords("fuck you", Scope::Slurs) == "fuck you");
	SetProfanityEnabled(false);
	return 0;
}
