#include "F4TWorldMode.h"

#include <cassert>

namespace
{
	using COUI::WorldMode::Label;
	using COUI::WorldMode::Normalize;
}

int main()
{
	assert(Normalize("coop-story") == "coop-story");
	assert(Normalize("faction-war") == "faction-war");
	assert(Normalize("instanced-story") == "instanced-story");
	assert(Normalize("free-roam") == "free-roam");

	assert(Normalize(" Free-Roam ") == "free-roam");
	assert(Normalize("FREE-ROAM") == "free-roam");
	assert(Normalize("\tfree-roam\n") == "free-roam");
	assert(Normalize("Coop-Story") == "coop-story");

	assert(Normalize("") == "");
	assert(Normalize("   ") == "");
	assert(Normalize("sandbox") == "");
	assert(Normalize("free_roam") == "");
	assert(Normalize("free-roam-x") == "");
	assert(Normalize("free") == "");

	assert(Normalize("Co-op") == "coop-story");
	assert(Normalize("co-op") == "coop-story");
	assert(Normalize("Co-op story") == "coop-story");
	assert(Normalize("Free Roam") == "free-roam");
	assert(Normalize("free roam") == "free-roam");
	assert(Normalize("Faction War") == "faction-war");
	assert(Normalize("Instanced Story") == "instanced-story");

	assert(Label(Normalize("Free Roam")) == "Free roam");
	assert(Label(Normalize("Co-op")) == "Co-op story");
	assert(Label(Normalize("faction war")) == "Faction war");
	assert(Label(Normalize("Instanced Story")) == "Instanced story");

	assert(Label("free-roam") == "Free roam");
	assert(Label("coop-story") == "Co-op story");
	assert(Label("faction-war") == "Faction war");
	assert(Label("instanced-story") == "Instanced story");
	assert(Label("") == "");
	assert(Label("sandbox") == "");

	assert(Label(Normalize("")).empty());
	assert(Label(Normalize("sandbox")).empty());
	assert(Label(Normalize(" Free-Roam ")) == "Free roam");

	return 0;
}
