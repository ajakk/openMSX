#include "ImGuiConsole.hh"

#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiUtils.hh"

#include "BooleanSetting.hh"
#include "CliComm.hh"
#include "Completer.hh"
#include "FileContext.hh"
#include "FileException.hh"
#include "FileOperations.hh"
#include "GlobalCommandController.hh"
#include "Interpreter.hh"
#include "Reactor.hh"
#include "TclParser.hh"
#include "Version.hh"

#include "narrow.hh"
#include "strCat.hh"
#include "utf8_unchecked.hh"
#include "xrange.hh"

#include <imgui.h>
#include <imgui_internal.h> /**/ // Hack: see below
#include <imgui_stdlib.h>

#include <fstream>

namespace openmsx {

using namespace std::literals;

static constexpr std::string_view PROMPT_NEW  = "> ";
static constexpr std::string_view PROMPT_CONT = "| ";
static constexpr std::string_view PROMPT_BUSY = "*busy*";

ImGuiConsole::ImGuiConsole(ImGuiManager& manager_)
	: manager(manager_)
	, consoleSetting(
		manager.getReactor().getCommandController(), "console",
		"turns console display on/off", false, Setting::DONT_SAVE)
	, history(1000)
	, lines(1000)
	, prompt(PROMPT_NEW)
{
	loadHistory();

	Completer::setOutput(this);
	manager.getInterpreter().setOutput(this);
	consoleSetting.attach(*this);

	const auto& fullVersion = Version::full();
	print(fullVersion);
	print(std::string(fullVersion.size(), '-'));
	print("\n"
	      "General information about openMSX is available at http://openmsx.org.\n"
	      "\n"
	      "Type 'help' to see a list of available commands.\n"
	      "Or read the Console Command Reference in the manual.\n"
	      "\n");
}

ImGuiConsole::~ImGuiConsole()
{
	consoleSetting.detach(*this);
}

void ImGuiConsole::save(ImGuiTextBuffer& buf)
{
	savePersistent(buf, *this, persistentElements);
}

void ImGuiConsole::loadLine(std::string_view name, zstring_view value)
{
	loadOnePersistent(name, value, *this, persistentElements);
}

void ImGuiConsole::print(std::string_view text, unsigned rgb)
{
	do {
		auto pos = text.find('\n');
		newLineConsole(ConsoleLine(std::string(text.substr(0, pos)), rgb));
		if (pos == std::string_view::npos) break;
		text.remove_prefix(pos + 1); // skip newline
	} while (!text.empty());
}

void ImGuiConsole::newLineConsole(ConsoleLine line)
{
	auto addLine = [&](ConsoleLine&& l) {
		if (lines.full()) lines.pop_front();
		lines.push_back(std::move(l));
	};

	if (wrap) {
		do {
			auto rest = line.splitAtColumn(columns);
			addLine(std::move(line));
			line = std::move(rest);
		} while (!line.str().empty());
	} else {
		addLine(std::move(line));
	}

	scrollToBottom = true;
}

static void drawLine(const ConsoleLine& line)
{
	auto n = line.numChunks();
	for (auto i : xrange(n)) {
		im::StyleColor(ImGuiCol_Text, line.chunkColor(i), [&]{
			ImGui::TextUnformatted(line.chunkText(i));
			if (i != (n - 1)) ImGui::SameLine(0.0f, 0.0f);
		});
	}
}

void ImGuiConsole::paint(MSXMotherBoard* /*motherBoard*/)
{
	bool reclaimFocus = show && !wasShown; // window appears
	wasShown = show;
	if (!show) return;

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	im::Window("Console", &show, [&]{
		// Reserve enough left-over height for 1 separator + 1 input text
		auto& style = ImGui::GetStyle();
		const float footerHeightToReserve = style.ItemSpacing.y +
						ImGui::GetFrameHeightWithSpacing();
		im::Child("ScrollingRegion",
		          ImVec2(0, -footerHeightToReserve), false,
		          ImGuiWindowFlags_HorizontalScrollbar, [&]{
			im::PopupContextWindow([&]{
				if (ImGui::Selectable("Clear")) {
					lines.clear();
				}
				ImGui::Checkbox("Wrap (new) output", &wrap);
			});

			im::StyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1), [&]{ // Tighten spacing
				im::ListClipper(lines.size(), [&](int i) {
					drawLine(lines[i]);
				});
			});

			// Keep up at the bottom of the scroll region if we were already
			// at the bottom at the beginning of the frame.
			if (scrollToBottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
				scrollToBottom = false;
				ImGui::SetScrollHereY(1.0f);
			}

			// recalculate the number of columns
			auto width = ImGui::GetContentRegionMax().x;
			auto charWidth = ImGui::CalcTextSize("M"sv).x;
			columns = narrow_cast<unsigned>(width / charWidth);
		});
		ImGui::Separator();

		// Command-line
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(prompt);
		ImGui::SameLine(0.0f, 0.0f);

		ImGui::SetNextItemWidth(-FLT_MIN); // full window width
		/**/ // Hack: see below
		/**/ auto cursorScrnPos = ImGui::GetCursorScreenPos();
		/**/ auto itemWidth = ImGui::CalcItemWidth();

		ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
					ImGuiInputTextFlags_EscapeClearsAll |
					ImGuiInputTextFlags_CallbackEdit |
					ImGuiInputTextFlags_CallbackCompletion |
					ImGuiInputTextFlags_CallbackHistory;
		if (ImGui::InputTextWithHint("##Input", "enter command", &inputBuf, flags, &textEditCallbackStub, this) &&
		(prompt != PROMPT_BUSY)) {
			// print command in output buffer, with prompt prepended
			ConsoleLine cmdLine(prompt);
			cmdLine.addLine(coloredInputBuf);
			newLineConsole(std::move(cmdLine));

			// append (partial) command to a possibly multi-line command
			strAppend(commandBuffer, inputBuf, '\n');

			putHistory(std::move(inputBuf));
			saveHistory(); // save at this point already, so that we don't lose history in case of a crash
			inputBuf.clear();
			coloredInputBuf.clear();
			historyPos = -1;
			historyBackupLine.clear();

			auto& commandController = manager.getReactor().getGlobalCommandController();
			if (commandController.isComplete(commandBuffer)) {
				// Normally the busy prompt is NOT shown (not even briefly
				// because the screen is not redrawn), though for some commands
				// that potentially take a long time to execute, we explicitly
				// do redraw.
				prompt = PROMPT_BUSY;

				manager.executeDelayed(TclObject(commandBuffer),
					[this](const TclObject& result) {
						if (const auto& s = result.getString(); !s.empty()) {
							this->print(s);
						}
						prompt = PROMPT_NEW;
					},
					[this](const std::string& error) {
						this->print(error, 0xff0000ff);
						prompt = PROMPT_NEW;
					});
				commandBuffer.clear();
			} else {
				prompt = PROMPT_CONT;
			}
			reclaimFocus = true;
		}
		ImGui::SetItemDefaultFocus();

		if (reclaimFocus ||
		(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
		!ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0) && !ImGui::IsMouseClicked(1))) {
			ImGui::SetKeyboardFocusHere(-1); // focus the InputText widget
		}

		/**/ // Hack: currently ImGui::InputText() does not support colored text.
		/**/ // Though there are plans to extend this. See:
		/**/ //     https://github.com/ocornut/imgui/pull/3130
		/**/ //     https://github.com/ocornut/imgui/issues/902
		/**/ // To work around this limitation, we use ImGui::InputText() as-is,
		/**/ // but then overdraw the text using the correct colors. This works,
		/**/ // but it's fragile because it depends on some internal implementation
		/**/ // details. More specifically: the scroll-position. And obtaining this
		/**/ // information required stuff from <imgui_internal.h>.
		/**/ auto* font = ImGui::GetFont();
		/**/ auto fontSize = ImGui::GetFontSize();
		/**/ gl::vec2 frameSize(itemWidth, fontSize + style.FramePadding.y * 2.0f);
		/**/ gl::vec2 topLeft = cursorScrnPos;
		/**/ gl::vec2 bottomRight = topLeft + frameSize;
		/**/ gl::vec2 drawPos = topLeft + gl::vec2(style.FramePadding);
		/**/ if (ImGui::IsItemActive()) {
		/**/	auto id = ImGui::GetID("##Input");
		/**/	if (auto* state = ImGui::GetInputTextState(id)) { // Internal API !!!
		/**/		drawPos[0] -= state->ScrollX;
		/**/	}
		/**/ }
		/**/ auto charWidth = ImGui::GetFont()->GetCharAdvance('A'); // assumes fixed-width font
		/**/ ImVec4 clipRect = gl::vec4(topLeft, bottomRight);
		/**/ auto* drawList = ImGui::GetWindowDrawList();
		/**/ for (auto i : xrange(coloredInputBuf.numChunks())) {
		/**/ 	auto text = coloredInputBuf.chunkText(i);
		/**/ 	auto rgba = coloredInputBuf.chunkColor(i);
		/**/ 	const char* begin = text.data();
		/**/ 	const char* end = begin + text.size();
		/**/ 	drawList->AddText(font, fontSize, drawPos, rgba, begin, end, 0.0f, &clipRect);
		/**/    // avoid ImGui::CalcTextSize(): it's off-by-one for sizes >= 256 pixels
		/**/    drawPos[0] += charWidth * utf8::unchecked::distance(begin, end);
		/**/ }
	});
}

int ImGuiConsole::textEditCallbackStub(ImGuiInputTextCallbackData* data)
{
	auto* console = static_cast<ImGuiConsole*>(data->UserData);
	return console->textEditCallback(data);
}

int ImGuiConsole::textEditCallback(ImGuiInputTextCallbackData* data)
{
	switch (data->EventFlag) {
	case ImGuiInputTextFlags_CallbackCompletion: {
		std::string_view oldLine{data->Buf, narrow<size_t>(data->BufTextLen)};
		std::string_view front = utf8::unchecked::substr(oldLine, 0, data->CursorPos);
		std::string_view back  = utf8::unchecked::substr(oldLine, data->CursorPos);

		auto& commandController = manager.getReactor().getGlobalCommandController();
		std::string newFront = commandController.tabCompletion(front);
		historyBackupLine = strCat(std::move(newFront), back);
		historyPos = -1;

		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, historyBackupLine.c_str());

		colorize(historyBackupLine);
		break;
	}
	case ImGuiInputTextFlags_CallbackHistory: {
		bool match = false;
		if (data->EventKey == ImGuiKey_UpArrow) {
			while (!match && (historyPos < narrow<int>(history.size() - 1))) {
				++historyPos;
				match = history[historyPos].starts_with(historyBackupLine);
			}
		} else if ((data->EventKey == ImGuiKey_DownArrow) && (historyPos != -1)) {
			while (!match) {
				if (--historyPos == -1) break;
				match = history[historyPos].starts_with(historyBackupLine);
			}
		}
		if (match || (historyPos == -1)) {
			const auto& historyStr = (historyPos >= 0) ? history[historyPos] : historyBackupLine;
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, historyStr.c_str());
			colorize(std::string_view{data->Buf, narrow<size_t>(data->BufTextLen)});
		}
		break;
	}
	case ImGuiInputTextFlags_CallbackEdit: {
		historyBackupLine.assign(data->Buf, narrow<size_t>(data->BufTextLen));
		historyPos = -1;
		colorize(historyBackupLine);
		break;
	}
	}
	return 0;
}

void ImGuiConsole::colorize(std::string_view line)
{
	TclParser parser = manager.getInterpreter().parse(line);
	const auto& colors = parser.getColors();
	assert(colors.size() == line.size());

	coloredInputBuf.clear();
	size_t pos = 0;
	while (pos != colors.size()) {
		char col = colors[pos];
		size_t pos2 = pos++;
		while ((pos != colors.size()) && (colors[pos] == col)) {
			++pos;
		}
		unsigned rgb = [&] {
			switch (col) {
			//                 AABBGGRR
			case 'E': return 0xff0000ff; // error
			case 'c': return 0xff5cff5c; // comment
			case 'v': return 0xffffff00; // variable
			case 'l': return 0xff00ffff; // literal
			case 'p': return 0xffcd00cd; // proc
			case 'o': return 0xffcdcd00; // operator
			default:  return 0xffffffff; // other
			}
		}();
		coloredInputBuf.addChunk(line.substr(pos2, pos - pos2), rgb);
	}
}

void ImGuiConsole::putHistory(std::string command)
{
	if (command.empty()) return;
	if (!history.empty() && (history.front() == command)) {
		return;
	}
	if (history.full()) history.pop_back();
	history.push_front(std::move(command));
}

void ImGuiConsole::saveHistory()
{
	try {
		std::ofstream outputFile;
		FileOperations::openOfStream(outputFile,
		        userFileContext("console").resolveCreate("history.txt"));
		if (!outputFile) {
			throw FileException("Error while saving the console history.");
		}
		for (auto& s : view::reverse(history)) {
			outputFile << s << '\n';
		}
	} catch (FileException& e) {
		manager.getCliComm().printWarning(e.getMessage());
	}
}

void ImGuiConsole::loadHistory()
{
	try {
		std::ifstream inputFile(
		        userFileContext("console").resolveCreate("history.txt"));
		std::string line;
		while (inputFile) {
			getline(inputFile, line);
			putHistory(line);
		}
	} catch (FileException&) {
		// Error while loading the console history, ignore
	}
}

void ImGuiConsole::output(std::string_view text)
{
	print(text);
}

unsigned ImGuiConsole::getOutputColumns() const
{
	return columns;
}

void ImGuiConsole::update(const Setting& /*setting*/) noexcept
{
	show = consoleSetting.getBoolean();
	if (!show) {
		// Close the console via the 'console' setting.  Typically this
		// means via the F10 hotkey (or possibly by typing 'set console
		// off' in the console).
		//
		// Give focus to the main openMSX window.
		//
		// This makes the following scenario work:
		// * You were controlling the MSX, e.g. playing a game.
		// * You press F10 to open the console.
		// * You type a command (e.g. swap a game disk, for some people
		//   the console is still more convenient and/or faster than the
		//   new media menu).
		// * You press F10 again to close the console
		// * At this point the focus should go back to the main openMSX
		//   window (so that MSX input works).
		SDL_SetWindowInputFocus(SDL_GetWindowFromID(WindowEvent::getMainWindowId()));
		ImGui::SetWindowFocus(nullptr);
	}
}

} // namespace openmsx
