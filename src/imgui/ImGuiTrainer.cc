#include "ImGuiTrainer.hh"

#include "CustomFont.h"
#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiUtils.hh"

#include "StringOp.hh"
#include "xrange.hh"

#include <imgui_stdlib.h>

#include <algorithm>

namespace openmsx {

using namespace std::literals;

void ImGuiTrainer::save(ImGuiTextBuffer& buf)
{
	savePersistent(buf, *this, persistentElements);
}

void ImGuiTrainer::loadLine(std::string_view name, zstring_view value)
{
	loadOnePersistent(name, value, *this, persistentElements);
}

void ImGuiTrainer::paint(MSXMotherBoard* /*motherBoard*/)
{
	if (!show) return;

	if (!trainers) {
		// Only query/sort this list once instead of on every frame.
		// If loading fails, use an empty dict (don't keep on retying).
		trainers = manager.execute(TclObject("trainer::load_trainers")).value_or(TclObject{});
		gameNames = to_vector(view::transform(xrange(trainers->size() / 2), [&](auto i) {
			return std::string(trainers->getListIndexUnchecked(2 * i).getString());
		}));
		ranges::sort(gameNames, StringOp::caseless{});
	}
	auto activeGame = manager.execute(makeTclList("set", "trainer::active_trainer")).value_or(TclObject{});
	auto activeList = manager.execute(makeTclList("set", "trainer::items_active")).value_or(TclObject{});

	std::optional<std::string> newGame;
	std::optional<int> toggleItem;
	bool all = false;
	bool none = false;

	ImGui::SetNextWindowSize(gl::vec2{28, 26} * ImGui::GetFontSize(), ImGuiCond_FirstUseEver);
	im::Window("Trainer Selector", &show, ImGuiWindowFlags_HorizontalScrollbar, [&]{
		ImGui::TextUnformatted("Select Game:"sv);
		zstring_view displayName = activeGame.getString();
		if (displayName.empty()) displayName = "none";
		bool useFilter = im::TreeNode("filter", [&]{
			ImGui::InputText(ICON_IGFD_SEARCH, &filterString);
			HelpMarker("A list of substrings that must be part of the game name.\n"
			           "\n"
			           "For example: enter 'vamp' to search for 'Akumajyo Drakyula - Vampire Killer'.");
		});
		auto drawGameNames = [&](size_t num, auto getName) {
			im::ListClipper(1 + num, [&](int i) {
				if (i == 0) {
					if (ImGui::Selectable("none", displayName == "none")) {
						newGame = "deactivate";
					}
				} else {
					auto name = getName(size_t(i - 1));
					if (ImGui::Selectable(name.c_str(), name == displayName)) {
						newGame = name;
					}
				}
			});
		};
		auto getGameName = [&](size_t i) { return gameNames[i]; };
		if (useFilter) {
			auto indices = to_vector(xrange(gameNames.size()));
			filterIndices(filterString, getGameName, indices);
			im::ListBox("##game", {-FLT_MIN, 0.0f}, [&]{
				drawGameNames(indices.size(), [&](size_t i) { return gameNames[indices[i]]; });
			});
		} else {
			ImGui::SetNextItemWidth(-FLT_MIN);
			im::Combo("##game", displayName.c_str(), [&]{
				drawGameNames(gameNames.size(), getGameName);
			});
		}
		ImGui::Separator();

		im::Disabled(activeGame.getString().empty(), [&]{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Select Cheats:"sv);
			ImGui::SameLine();
			all = ImGui::Button("All");
			ImGui::SameLine();
			none = ImGui::Button("None");

			TclObject items = trainers->getOptionalDictValue(activeGame).value_or(TclObject{})
				.getOptionalDictValue(TclObject("items")).value_or(TclObject{});
			auto numItems = std::min(activeList.size(), items.size() / 2);
			for (auto i : xrange(numItems)) {
				bool active = activeList.getListIndexUnchecked(i).getOptionalBool().value_or(false);
				auto name = items.getListIndexUnchecked(2 * i + 0).getString();
				if (ImGui::Checkbox(name.c_str(), &active)) {
					toggleItem = i;
				}
			}
		});
	});

	if (newGame) {
		manager.execute(makeTclList("trainer", *newGame));
	} else if (toggleItem) {
		manager.execute(makeTclList("trainer", activeGame, *toggleItem + 1));
	} else if (all || none) {
		auto cmd = makeTclList("trainer", activeGame);
		for (auto i : xrange(activeList.size())) {
			if (activeList.getListIndexUnchecked(i).getOptionalBool().value_or(false) == none) {
				cmd.addListElement(i + 1);
			}
		}
		manager.execute(cmd);
	}
}

} // namespace openmsx
