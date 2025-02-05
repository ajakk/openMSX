#include "ImGuiSettings.hh"

#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiUtils.hh"

#include "BooleanInput.hh"
#include "BooleanSetting.hh"
#include "CPUCore.hh"
#include "Display.hh"
#include "EventDistributor.hh"
#include "FilenameSetting.hh"
#include "FloatSetting.hh"
#include "GlobalCommandController.hh"
#include "GlobalSettings.hh"
#include "InputEventFactory.hh"
#include "IntegerSetting.hh"
#include "JoyMega.hh"
#include "KeyCodeSetting.hh"
#include "KeyboardSettings.hh"
#include "Mixer.hh"
#include "MSXCPU.hh"
#include "MSXCommandController.hh"
#include "MSXJoystick.hh"
#include "MSXMotherBoard.hh"
#include "ProxySetting.hh"
#include "R800.hh"
#include "Reactor.hh"
#include "ReadOnlySetting.hh"
#include "SettingsManager.hh"
#include "StringSetting.hh"
#include "Version.hh"
#include "VideoSourceSetting.hh"
#include "Z80.hh"

#include "checked_cast.hh"
#include "enumerate.hh"
#include "foreach_file.hh"
#include "join.hh"
#include "narrow.hh"
#include "view.hh"
#include "StringOp.hh"
#include "unreachable.hh"
#include "zstring_view.hh"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <SDL.h>

#include <optional>

using namespace std::literals;

namespace openmsx {

ImGuiSettings::ImGuiSettings(ImGuiManager& manager_)
	: manager(manager_)
{
}

ImGuiSettings::~ImGuiSettings()
{
	deinitListener();
}

void ImGuiSettings::save(ImGuiTextBuffer& buf)
{
	savePersistent(buf, *this, persistentElements);
}

void ImGuiSettings::loadLine(std::string_view name, zstring_view value)
{
	loadOnePersistent(name, value, *this, persistentElements);
}

void ImGuiSettings::loadEnd()
{
	setStyle();
}

void ImGuiSettings::setStyle()
{
	switch (selectedStyle) {
	case 0: ImGui::StyleColorsDark();    break;
	case 1: ImGui::StyleColorsLight();   break;
	case 2: ImGui::StyleColorsClassic(); break;
	}
}
void ImGuiSettings::showMenu(MSXMotherBoard* motherBoard)
{
	bool openConfirmPopup = false;

	im::Menu("Settings", [&]{
		auto& reactor = manager.getReactor();
		auto& globalSettings = reactor.getGlobalSettings();
		auto& renderSettings = reactor.getDisplay().getRenderSettings();
		auto& settingsManager = reactor.getGlobalCommandController().getSettingsManager();
		const auto& hotKey = reactor.getHotKey();

		im::Menu("Video", [&]{
			im::TreeNode("Look and feel", ImGuiTreeNodeFlags_DefaultOpen, [&]{
				auto& scaler = renderSettings.getScaleAlgorithmSetting();
				ComboBox("Scaler", scaler);
				im::Indent([&]{
					struct AlgoEnable {
						RenderSettings::ScaleAlgorithm algo;
						bool hasScanline;
						bool hasBlur;
					};
					static constexpr std::array algoEnables = {
						//                                        scanline / blur
						AlgoEnable{RenderSettings::SCALER_SIMPLE,     true,  true },
						AlgoEnable{RenderSettings::SCALER_SCALE,      false, false},
						AlgoEnable{RenderSettings::SCALER_HQ,         false, false},
						AlgoEnable{RenderSettings::SCALER_HQLITE,     false, false},
						AlgoEnable{RenderSettings::SCALER_RGBTRIPLET, true,  true },
						AlgoEnable{RenderSettings::SCALER_TV,         true,  false},
					};
					auto it = ranges::find(algoEnables, scaler.getEnum(), &AlgoEnable::algo);
					assert(it != algoEnables.end());
					im::Disabled(!it->hasScanline, [&]{
						SliderInt("Scanline (%)", renderSettings.getScanlineSetting());
					});
					im::Disabled(!it->hasBlur, [&]{
						SliderInt("Blur (%)", renderSettings.getBlurSetting());
					});
				});

				SliderInt("Scale factor", renderSettings.getScaleFactorSetting());
				Checkbox(hotKey, "Deinterlace", renderSettings.getDeinterlaceSetting());
				Checkbox(hotKey, "Deflicker", renderSettings.getDeflickerSetting());
			});
			im::TreeNode("Colors", ImGuiTreeNodeFlags_DefaultOpen, [&]{
				SliderFloat("Noise (%)", renderSettings.getNoiseSetting());
				SliderFloat("Brightness", renderSettings.getBrightnessSetting());
				SliderFloat("Contrast", renderSettings.getContrastSetting());
				SliderFloat("Gamma", renderSettings.getGammaSetting());
				SliderInt("Glow (%)", renderSettings.getGlowSetting());
				if (auto* monitor = dynamic_cast<Setting*>(settingsManager.findSetting("monitor_type"))) {
					ComboBox("Monitor type", *monitor, [](std::string s) {
						ranges::replace(s, '_', ' ');
						return s;
					});
				}
			});
			im::TreeNode("Shape", ImGuiTreeNodeFlags_DefaultOpen, [&]{
				SliderFloat("Horizontal stretch", renderSettings.getHorizontalStretchSetting(), "%.0f");
				ComboBox("Display deformation", renderSettings.getDisplayDeformSetting());
			});
			im::TreeNode("Misc", ImGuiTreeNodeFlags_DefaultOpen, [&]{
				Checkbox(hotKey, "Full screen", renderSettings.getFullScreenSetting());
				if (motherBoard) {
					ComboBox("Video source to display", motherBoard->getVideoSource());
				}
				Checkbox(hotKey, "VSync", renderSettings.getVSyncSetting());
				SliderInt("Minimum frame-skip", renderSettings.getMinFrameSkipSetting()); // TODO: either leave out this setting, or add a tooltip like, "Leave on 0 unless you use a very slow device and want regular frame skipping");
				SliderInt("Maximum frame-skip", renderSettings.getMaxFrameSkipSetting()); // TODO: either leave out this setting or add a tooltip like  "On slow devices, skip no more than this amount of frames to keep emulation on time.");
			});
			im::TreeNode("Advanced (for debugging)", [&]{ // default collapsed
				Checkbox(hotKey, "Enforce VDP sprites-per-line limit", renderSettings.getLimitSpritesSetting());
				Checkbox(hotKey, "Disable sprites", renderSettings.getDisableSpritesSetting());
				ComboBox("Way to handle too fast VDP access", renderSettings.getTooFastAccessSetting());
				ComboBox("Emulate VDP command timing", renderSettings.getCmdTimingSetting());
			});
		});
		im::Menu("Sound", [&]{
			auto& mixer = reactor.getMixer();
			auto& muteSetting = mixer.getMuteSetting();
			im::Disabled(muteSetting.getBoolean(), [&]{
				SliderInt("Master volume", mixer.getMasterVolume());
			});
			Checkbox(hotKey, "Mute", muteSetting);
			ImGui::Separator();
			static constexpr std::array resamplerToolTips = {
				EnumToolTip{"hq",   "best quality, uses more CPU"},
				EnumToolTip{"blip", "good speed/quality tradeoff"},
				EnumToolTip{"fast", "fast but low quality"},
			};
			ComboBox("Resampler", globalSettings.getResampleSetting(), resamplerToolTips);
			ImGui::Separator();

			ImGui::MenuItem("Show sound chip settings", nullptr, &manager.soundChip.showSoundChipSettings);
		});
		im::Menu("Speed", [&]{
			im::TreeNode("Emulation", ImGuiTreeNodeFlags_DefaultOpen, [&]{
				ImGui::SameLine();
				HelpMarker("These control the speed of the whole MSX machine, "
				           "the running MSX software can't tell the difference.");

				auto& speedManager = globalSettings.getSpeedManager();
				auto& fwdSetting = speedManager.getFastForwardSetting();
				int fastForward = fwdSetting.getBoolean() ? 1 : 0;
				ImGui::TextUnformatted("Speed:"sv);
				ImGui::SameLine();
				bool fwdChanged = ImGui::RadioButton("normal", &fastForward, 0);
				ImGui::SameLine();
				fwdChanged |= ImGui::RadioButton("fast forward", &fastForward, 1);
				auto fastForwardShortCut = getShortCutForCommand(reactor.getHotKey(), "toggle fastforward");
				if (!fastForwardShortCut.empty()) {
					HelpMarker(strCat("Use '", fastForwardShortCut ,"' to quickly toggle between these two"));
				}
				if (fwdChanged) {
					fwdSetting.setBoolean(fastForward != 0);
				}
				im::Indent([&]{
					im::Disabled(fastForward != 0, [&]{
						SliderInt("Speed (%)", speedManager.getSpeedSetting());
					});
					im::Disabled(fastForward != 1, [&]{
						SliderInt("Fast forward speed (%)", speedManager.getFastForwardSpeedSetting());
					});
				});
				Checkbox(hotKey, "Go full speed when loading", globalSettings.getThrottleManager().getFullSpeedLoadingSetting());
			});
			if (motherBoard) {
				im::TreeNode("MSX devices", ImGuiTreeNodeFlags_DefaultOpen, [&]{
					ImGui::SameLine();
					HelpMarker("These control the speed of the specific components in the MSX machine. "
						"So the relative speed between components can change. "
						"And this may lead the emulation problems.");

					MSXCPU& cpu = motherBoard->getCPU();
					auto showFreqSettings = [&](std::string_view name, auto* core) {
						if (!core) return;
						auto& locked = core->getFreqLockedSetting();
						auto& value = core->getFreqValueSetting();
						// Note: GUI shows "UNlocked", while the actual settings is "locked"
						bool unlocked = !locked.getBoolean();
						if (ImGui::Checkbox(tmpStrCat("unlock custom ", name, " frequency").c_str(), &unlocked)) {
							locked.setBoolean(!unlocked);
						}
						simpleToolTip([&]{ return locked.getDescription(); });
						im::Indent([&]{
							im::Disabled(!unlocked, [&]{
								float fval = float(value.getInt()) / 1.0e6f;
								if (ImGui::InputFloat(tmpStrCat("frequency (MHz)##", name).c_str(), &fval, 0.01f, 1.0f, "%.2f")) {
									value.setInt(int(fval * 1.0e6f));
								}
								im::PopupContextItem(tmpStrCat("freq-context##", name).c_str(), [&]{
									const char* F358 = name == "Z80" ? "3.58 MHz (default)"
									                                 : "3.58 MHz";
									if (ImGui::Selectable(F358)) {
										value.setInt(3'579'545);
									}
									if (ImGui::Selectable("5.37 MHz")) {
										value.setInt(5'369'318);
									}
									const char* F716 = name == "R800" ? "7.16 MHz (default)"
									                                  : "7.16 MHz";
									if (ImGui::Selectable(F716)) {
										value.setInt(7'159'090);
									}

								});
								HelpMarker("Right-click to select commonly used values");
							});
						});
					};
					showFreqSettings("Z80",  cpu.getZ80());
					showFreqSettings("R800", cpu.getR800()); // might be nullptr
				});
			}
		});
		im::Menu("Input", [&]{
			static constexpr std::array kbdModeToolTips = {
				EnumToolTip{"CHARACTER",  "Tries to understand the character you are typing and then attempts to type that character using the current MSX keyboard. May not work very well when using a non-US host keyboard."},
				EnumToolTip{"KEY",        "Tries to map a key you press to the corresponding MSX key"},
				EnumToolTip{"POSITIONAL", "Tries to map the keyboard key positions to the MSX keyboard key positions"},
			};
			if (motherBoard) {
				auto& controller = motherBoard->getMSXCommandController();
				if (auto* turbo = dynamic_cast<IntegerSetting*>(controller.findSetting("renshaturbo"))) {
					SliderInt("Ren Sha Turbo (%)", *turbo);
				}
				if (auto* mappingModeSetting = dynamic_cast<EnumSetting<KeyboardSettings::MappingMode>*>(controller.findSetting("kbd_mapping_mode"))) {
					ComboBox("Keyboard mapping mode", *mappingModeSetting, kbdModeToolTips);
				}
			};
			ImGui::MenuItem("Configure MSX joysticks...", nullptr, &showConfigureJoystick);
		});
		im::Menu("GUI", [&]{
			im::Menu("Save layout ...", [&]{
				ImGui::TextUnformatted("Enter name:"sv);
				ImGui::InputText("##save-layout-name", &saveLayoutName);
				ImGui::SameLine();
				im::Disabled(saveLayoutName.empty(), [&]{
					if (ImGui::Button("Create")) {
						ImGui::CloseCurrentPopup();

						auto filename = FileOperations::parseCommandFileArgument(
							saveLayoutName, "layouts", "", ".ini");
						if (FileOperations::exists(filename)) {
							confirmText = strCat("Overwrite layout: ", saveLayoutName);
							confirmAction = [filename]{
								ImGui::SaveIniSettingsToDisk(filename.c_str());
							};
							openConfirmPopup = true;
						} else {
							ImGui::SaveIniSettingsToDisk(filename.c_str());
						}
					}
				});
			});
			im::Menu("Restore layout ...", [&]{
				ImGui::TextUnformatted("Select layout"sv);
				im::ListBox("##select-layout", [&]{
					std::vector<std::string> names;
					auto context = userDataFileContext("layouts");
					for (const auto& path : context.getPaths()) {
						foreach_file(path, [&](const std::string& fullName, std::string_view name) {
							if (name.ends_with(".ini")) {
								names.emplace_back(fullName);
							}
						});
					}
					ranges::sort(names, StringOp::caseless{});
					for (const auto& name : names) {
						auto displayName = std::string(FileOperations::stripExtension(FileOperations::getFilename(name)));
						if (ImGui::Selectable(displayName.c_str())) {
							manager.loadIniFile = name;
							ImGui::CloseCurrentPopup();
						}
						im::PopupContextItem([&]{
							if (ImGui::MenuItem("delete")) {
								confirmText = strCat("Delete layout: ", displayName);
								confirmAction = [name]{ FileOperations::unlink(name); };
								openConfirmPopup = true;
							}
						});
					}
				});
			});
			im::Menu("Select Style", [&]{
				std::optional<int> newStyle;
				std::array names = {"Dark", "Light", "Classic"}; // must be in sync with setStyle()
				for (auto i : xrange(narrow<int>(names.size()))) {
					if (ImGui::Selectable(names[i], selectedStyle == i)) {
						newStyle = i;
					}
				}
				if (newStyle) {
					selectedStyle = *newStyle;
					setStyle();
				}
			});
		});
		im::Menu("Misc", [&]{
			ImGui::MenuItem("Configure OSD icons...", nullptr, &manager.osdIcons.showConfigureIcons);
			ImGui::MenuItem("Fade out menu bar", nullptr, &manager.menuFade);
			ImGui::MenuItem("Configure messages...", nullptr, &manager.messages.showConfigure);
		});
		ImGui::Separator();
		im::Menu("Advanced", [&]{
			ImGui::TextUnformatted("All settings"sv);
			ImGui::Separator();
			std::vector<Setting*> settings;
			for (auto* setting : settingsManager.getAllSettings()) {
				if (dynamic_cast<ProxySetting*>(setting)) continue;
				if (dynamic_cast<ReadOnlySetting*>(setting)) continue;
				settings.push_back(checked_cast<Setting*>(setting));
			}
			ranges::sort(settings, StringOp::caseless{}, &Setting::getBaseName);
			for (auto* setting : settings) {
				if (auto* bSetting = dynamic_cast<BooleanSetting*>(setting)) {
					Checkbox(hotKey, *bSetting);
				} else if (auto* iSetting = dynamic_cast<IntegerSetting*>(setting)) {
					SliderInt(*iSetting);
				} else if (auto* fSetting = dynamic_cast<FloatSetting*>(setting)) {
					SliderFloat(*fSetting);
				} else if (auto* sSetting = dynamic_cast<StringSetting*>(setting)) {
					InputText(*sSetting);
				} else if (auto* fnSetting = dynamic_cast<FilenameSetting*>(setting)) {
					InputText(*fnSetting); // TODO
				} else if (auto* kSetting = dynamic_cast<KeyCodeSetting*>(setting)) {
					InputText(*kSetting); // TODO
				} else if (dynamic_cast<EnumSettingBase*>(setting)) {
					ComboBox(*setting);
				} else if (auto* vSetting = dynamic_cast<VideoSourceSetting*>(setting)) {
					ComboBox(*vSetting);
				} else {
					assert(false);
				}
			}
			if (!Version::RELEASE) {
				ImGui::Separator();
				ImGui::Checkbox("ImGui Demo Window", &showDemoWindow);
				HelpMarker("Show the ImGui demo window.\n"
					"This is purely to demonstrate the ImGui capabilities.\n"
					"There is no connection with any openMSX functionality.");
			}
		});
	});
	if (showDemoWindow) {
		ImGui::ShowDemoWindow(&showDemoWindow);
	}

	const auto confirmTitle = "Confirm##settings";
	if (openConfirmPopup) {
		ImGui::OpenPopup(confirmTitle);
	}
	im::PopupModal(confirmTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize, [&]{
		ImGui::TextUnformatted(confirmText);

		bool close = false;
		if (ImGui::Button("Ok")) {
			confirmAction();
			close = true;
		}
		ImGui::SameLine();
		close |= ImGui::Button("Cancel");
		if (close) {
			ImGui::CloseCurrentPopup();
			confirmAction = {};
		}
	});
}

////// joystick stuff

// joystick is 0..3
[[nodiscard]] static std::string settingName(unsigned joystick)
{
	return (joystick < 2) ? strCat("msxjoystick", joystick + 1, "_config")
	                      : strCat("joymega", joystick - 1, "_config");
}

// joystick is 0..3
[[nodiscard]] static std::string joystickToGuiString(unsigned joystick)
{
	return (joystick < 2) ? strCat("MSX joystick ", joystick + 1)
	                      : strCat("JoyMega controller ", joystick - 1);
}

[[nodiscard]] static std::string toGuiString(const BooleanInput& input)
{
	return std::visit(overloaded{
		[](const BooleanKeyboard& k) {
			return strCat("keyboard key ", SDLKey::toString(k.getKeyCode()));
		},
		[](const BooleanMouseButton& m) {
			return strCat("mouse button ", m.getButton());
		},
		[](const BooleanJoystickButton& j) {
			return strCat(SDL_JoystickNameForIndex(j.getJoystick()), " button ", j.getButton());
		},
		[](const BooleanJoystickHat& h) {
			const char* str = [&] {
				switch (h.getValue()) {
					case BooleanJoystickHat::UP:    return "up";
					case BooleanJoystickHat::RIGHT: return "right";
					case BooleanJoystickHat::DOWN:  return "down";
					case BooleanJoystickHat::LEFT:  return "left";
					default: UNREACHABLE; return "";
				}
			}();
			return strCat(SDL_JoystickNameForIndex(h.getJoystick()), " D-pad ", h.getHat(), ' ', str);
		},
		[](const BooleanJoystickAxis& a) {
			return strCat(SDL_JoystickNameForIndex(a.getJoystick()),
			              " stick axis ", a.getAxis(), ", ",
			              (a.getDirection() == BooleanJoystickAxis::POS ? "positive" : "negative"), " direction");
		}
	}, input);
}

[[nodiscard]] static bool insideCircle(gl::vec2 mouse, gl::vec2 center, float radius)
{
	auto delta = center - mouse;
	return gl::sum(delta * delta) <= (radius * radius);
}
[[nodiscard]] static bool between(float x, float min, float max)
{
	return (min <= x) && (x <= max);
}

struct Rectangle {
	gl::vec2 topLeft;
	gl::vec2 bottomRight;
};
[[nodiscard]] static bool insideRectangle(gl::vec2 mouse, Rectangle r)
{
	return between(mouse[0], r.topLeft[0], r.bottomRight[0]) &&
	       between(mouse[1], r.topLeft[1], r.bottomRight[1]);
}


static constexpr auto white = uint32_t(0xffffffff);
static constexpr auto fractionDPad = 1.0f / 3.0f;
static constexpr auto thickness = 3.0f;

static void drawDPad(gl::vec2 center, float size, std::span<const uint8_t, 4> hovered, int hoveredRow)
{
	const auto F = fractionDPad;
	std::array<std::array<ImVec2, 5 + 1>, 4> points = {
		std::array<ImVec2, 5 + 1>{ // UP
			center + size * gl::vec2{ 0,  0},
			center + size * gl::vec2{-F, -F},
			center + size * gl::vec2{-F, -1},
			center + size * gl::vec2{ F, -1},
			center + size * gl::vec2{ F, -F},
			center + size * gl::vec2{ 0,  0},
		},
		std::array<ImVec2, 5 + 1>{ // DOWN
			center + size * gl::vec2{ 0,  0},
			center + size * gl::vec2{ F,  F},
			center + size * gl::vec2{ F,  1},
			center + size * gl::vec2{-F,  1},
			center + size * gl::vec2{-F,  F},
			center + size * gl::vec2{ 0,  0},
		},
		std::array<ImVec2, 5 + 1>{ // LEFT
			center + size * gl::vec2{ 0,  0},
			center + size * gl::vec2{-F,  F},
			center + size * gl::vec2{-1,  F},
			center + size * gl::vec2{-1, -F},
			center + size * gl::vec2{-F, -F},
			center + size * gl::vec2{ 0,  0},
		},
		std::array<ImVec2, 5 + 1>{ // RIGHT
			center + size * gl::vec2{ 0,  0},
			center + size * gl::vec2{ F, -F},
			center + size * gl::vec2{ 1, -F},
			center + size * gl::vec2{ 1,  F},
			center + size * gl::vec2{ F,  F},
			center + size * gl::vec2{ 0,  0},
		},
	};

	auto* drawList = ImGui::GetWindowDrawList();
	auto hoverColor = ImGui::GetColorU32(ImGuiCol_ButtonHovered);

	for (auto i : xrange(4)) {
		if (hovered[i] || (hoveredRow == i)) {
			drawList->AddConvexPolyFilled(points[i].data(), 5, hoverColor);
		}
		drawList->AddPolyline(points[i].data(), 5 + 1, white, 0, thickness);
	}
}

static void drawFilledCircle(gl::vec2 center, float radius, bool fill)
{
	auto* drawList = ImGui::GetWindowDrawList();
	if (fill) {
		auto hoverColor = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
		drawList->AddCircleFilled(center, radius, hoverColor);
	}
	drawList->AddCircle(center, radius, white, 0, thickness);
}
static void drawFilledRectangle(Rectangle r, float corner, bool fill)
{
	auto* drawList = ImGui::GetWindowDrawList();
	if (fill) {
		auto hoverColor = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
		drawList->AddRectFilled(r.topLeft, r.bottomRight, hoverColor, corner);
	}
	drawList->AddRect(r.topLeft, r.bottomRight, white, corner, 0, thickness);
}

static void drawLetterA(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	const std::array<ImVec2, 3> lines = { tr({-6, 7}), tr({0, -7}), tr({6, 7}) };
	drawList->AddPolyline(lines.data(), lines.size(), white, 0, thickness);
	drawList->AddLine(tr({-3, 1}), tr({3, 1}), white, thickness);
}
static void drawLetterB(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	const std::array<ImVec2, 4> lines = { tr({1, -7}), tr({-4, -7}), tr({-4, 7}), tr({2, 7}) };
	drawList->AddPolyline(lines.data(), lines.size(), white, 0, thickness);
	drawList->AddLine(tr({-4, -1}), tr({2, -1}), white, thickness);
	drawList->AddBezierQuadratic(tr({1, -7}), tr({4, -7}), tr({4, -4}), white, thickness);
	drawList->AddBezierQuadratic(tr({4, -4}), tr({4, -1}), tr({1, -1}), white, thickness);
	drawList->AddBezierQuadratic(tr({2, -1}), tr({6, -1}), tr({6,  3}), white, thickness);
	drawList->AddBezierQuadratic(tr({6,  3}), tr({6,  7}), tr({2,  7}), white, thickness);
}
static void drawLetterC(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	drawList->AddBezierCubic(tr({5, -5}), tr({-8, -16}), tr({-8, 16}), tr({5, 5}), white, thickness);
}
static void drawLetterX(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	drawList->AddLine(tr({-4, -6}), tr({4,  6}), white, thickness);
	drawList->AddLine(tr({-4,  6}), tr({4, -6}), white, thickness);
}
static void drawLetterY(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	drawList->AddLine(tr({-4, -6}), tr({0,  0}), white, thickness);
	drawList->AddLine(tr({-4,  6}), tr({4, -6}), white, thickness);
}
static void drawLetterZ(gl::vec2 center)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return center + p; };
	const std::array<ImVec2, 4> linesZ2 = { tr({-4, -6}), tr({4, -6}), tr({-4, 6}), tr({4, 6}) };
	drawList->AddPolyline(linesZ2.data(), 4, white, 0, thickness);
}

namespace msxjoystick {

enum {UP, DOWN, LEFT, RIGHT, TRIG_A, TRIG_B, NUM_BUTTONS, NUM_DIRECTIONS = TRIG_A};

static constexpr std::array<zstring_view, NUM_BUTTONS> buttonNames = {
	"Up", "Down", "Left", "Right", "A", "B" // show in the GUI
};
static constexpr std::array<zstring_view, NUM_BUTTONS> keyNames = {
	"UP", "DOWN", "LEFT", "RIGHT", "A", "B" // keys in Tcl dict
};

// Customize joystick look
static constexpr auto boundingBox = gl::vec2{300.0f, 100.0f};
static constexpr auto radius = 20.0f;
static constexpr auto corner = 10.0f;
static constexpr auto centerA = gl::vec2{200.0f, 50.0f};
static constexpr auto centerB = gl::vec2{260.0f, 50.0f};
static constexpr auto centerDPad = gl::vec2{50.0f, 50.0f};
static constexpr auto sizeDPad = 30.0f;

[[nodiscard]] static std::vector<uint8_t> buttonsHovered(gl::vec2 mouse)
{
	std::vector<uint8_t> result(NUM_BUTTONS); // false
	auto mouseDPad = (mouse - centerDPad) * (1.0f / sizeDPad);
	if (insideRectangle(mouseDPad, Rectangle{{-1, -1}, {1, 1}}) &&
		(between(mouseDPad[0], -fractionDPad, fractionDPad) ||
		between(mouseDPad[1], -fractionDPad, fractionDPad))) { // mouse over d-pad
		bool t1 = mouseDPad[0] <  mouseDPad[1];
		bool t2 = mouseDPad[0] < -mouseDPad[1];
		result[UP]    = !t1 &&  t2;
		result[DOWN]  =  t1 && !t2;
		result[LEFT]  =  t1 &&  t2;
		result[RIGHT] = !t1 && !t2;
	}
	result[TRIG_A] = insideCircle(mouse, centerA, radius);
	result[TRIG_B] = insideCircle(mouse, centerB, radius);
	return result;
}

static void draw(gl::vec2 scrnPos, std::span<uint8_t> hovered, int hoveredRow)
{
	auto* drawList = ImGui::GetWindowDrawList();

	drawList->AddRect(scrnPos, scrnPos + boundingBox, white, corner, 0, thickness);

	drawDPad(scrnPos + centerDPad, sizeDPad, subspan<4>(hovered), hoveredRow);

	auto scrnCenterA = scrnPos + centerA;
	drawFilledCircle(scrnCenterA, radius, hovered[TRIG_A] || (hoveredRow == TRIG_A));
	drawLetterA(scrnCenterA);

	auto scrnCenterB = scrnPos + centerB;
	drawFilledCircle(scrnCenterB, radius, hovered[TRIG_B] || (hoveredRow == TRIG_B));
	drawLetterB(scrnCenterB);
}

} // namespace msxjoystick

namespace joymega {

enum {UP, DOWN, LEFT, RIGHT,
      TRIG_A, TRIG_B, TRIG_C,
      TRIG_X, TRIG_Y, TRIG_Z,
      TRIG_SELECT, TRIG_START,
      NUM_BUTTONS, NUM_DIRECTIONS = TRIG_A};

static constexpr std::array<zstring_view, NUM_BUTTONS> buttonNames = { // show in the GUI
	"Up", "Down", "Left", "Right",
	"A", "B", "C",
	"X", "Y", "Z",
	"Select", "Start",
};
static constexpr std::array<zstring_view, NUM_BUTTONS> keyNames = { // keys in Tcl dict
	"UP", "DOWN", "LEFT", "RIGHT",
	"A", "B", "C",
	"X", "Y", "Z",
	"SELECT", "START",
};

// Customize joystick look
static constexpr auto thickness = 3.0f;
static constexpr auto boundingBox = gl::vec2{300.0f, 158.0f};
static constexpr auto centerA = gl::vec2{205.0f, 109.9f};
static constexpr auto centerB = gl::vec2{235.9f,  93.5f};
static constexpr auto centerC = gl::vec2{269.7f,  83.9f};
static constexpr auto centerX = gl::vec2{194.8f,  75.2f};
static constexpr auto centerY = gl::vec2{223.0f,  61.3f};
static constexpr auto centerZ = gl::vec2{252.2f,  52.9f};
static constexpr auto selectBox = Rectangle{gl::vec2{130.0f, 60.0f}, gl::vec2{160.0f, 70.0f}};
static constexpr auto startBox  = Rectangle{gl::vec2{130.0f, 86.0f}, gl::vec2{160.0f, 96.0f}};
static constexpr auto radiusABC = 16.2f;
static constexpr auto radiusXYZ = 12.2f;
static constexpr auto centerDPad = gl::vec2{65.6f, 82.7f};
static constexpr auto sizeDPad = 34.0f;
static constexpr auto fractionDPad = 1.0f / 3.0f;

[[nodiscard]] static std::vector<uint8_t> buttonsHovered(gl::vec2 mouse)
{
	std::vector<uint8_t> result(NUM_BUTTONS); // false
	auto mouseDPad = (mouse - centerDPad) * (1.0f / sizeDPad);
	if (insideRectangle(mouseDPad, Rectangle{{-1, -1}, {1, 1}}) &&
		(between(mouseDPad[0], -fractionDPad, fractionDPad) ||
		between(mouseDPad[1], -fractionDPad, fractionDPad))) { // mouse over d-pad
		bool t1 = mouseDPad[0] <  mouseDPad[1];
		bool t2 = mouseDPad[0] < -mouseDPad[1];
		result[UP]    = !t1 &&  t2;
		result[DOWN]  =  t1 && !t2;
		result[LEFT]  =  t1 &&  t2;
		result[RIGHT] = !t1 && !t2;
	}
	result[TRIG_A] = insideCircle(mouse, centerA, radiusABC);
	result[TRIG_B] = insideCircle(mouse, centerB, radiusABC);
	result[TRIG_C] = insideCircle(mouse, centerC, radiusABC);
	result[TRIG_X] = insideCircle(mouse, centerX, radiusXYZ);
	result[TRIG_Y] = insideCircle(mouse, centerY, radiusXYZ);
	result[TRIG_Z] = insideCircle(mouse, centerZ, radiusXYZ);
	result[TRIG_START]  = insideRectangle(mouse, startBox);
	result[TRIG_SELECT] = insideRectangle(mouse, selectBox);
	return result;
}

static void draw(gl::vec2 scrnPos, std::span<uint8_t> hovered, int hoveredRow)
{
	auto* drawList = ImGui::GetWindowDrawList();
	auto tr = [&](gl::vec2 p) { return scrnPos + p; };

	auto drawBezierCurve = [&](std::span<const gl::vec2> points, float thick = 1.0f) {
		assert((points.size() % 2) == 0);
		for (size_t i = 0; i < points.size() - 2; i += 2) {
			auto ap = points[i + 0];
			auto ad = points[i + 1];
			auto bp = points[i + 2];
			auto bd = points[i + 3];
			drawList->AddBezierCubic(tr(ap), tr(ap + ad), tr(bp - bd), tr(bp), white, thick);
		}
	};

	std::array outLine = {
		gl::vec2{150.0f,   0.0f}, gl::vec2{ 23.1f,   0.0f},
		gl::vec2{258.3f,  30.3f}, gl::vec2{ 36.3f,  26.4f},
		gl::vec2{300.0f, 107.0f}, gl::vec2{  0.0f,  13.2f},
		gl::vec2{285.2f, 145.1f}, gl::vec2{ -9.9f,   9.9f},
		gl::vec2{255.3f, 157.4f}, gl::vec2{ -9.0f,   0.0f},
		gl::vec2{206.0f, 141.8f}, gl::vec2{-16.2f,  -5.6f},
		gl::vec2{150.0f, 131.9f}, gl::vec2{-16.5f,   0.0f},
		gl::vec2{ 94.0f, 141.8f}, gl::vec2{-16.2f,   5.6f},
		gl::vec2{ 44.7f, 157.4f}, gl::vec2{ -9.0f,   0.0f},
		gl::vec2{ 14.8f, 145.1f}, gl::vec2{ -9.9f,  -9.9f},
		gl::vec2{  0.0f, 107.0f}, gl::vec2{  0.0f, -13.2f},
		gl::vec2{ 41.7f,  30.3f}, gl::vec2{ 36.3f, -26.4f},
		gl::vec2{150.0f,   0.0f}, gl::vec2{ 23.1f,   0.0f}, // closed loop
	};
	drawBezierCurve(outLine, thickness);

	drawDPad(tr(centerDPad), sizeDPad, subspan<4>(hovered), hoveredRow);
	drawList->AddCircle(tr(centerDPad), 43.0f, white);
	std::array dPadCurve = {
		gl::vec2{77.0f,  33.0f}, gl::vec2{ 69.2f, 0.0f},
		gl::vec2{54.8f, 135.2f}, gl::vec2{-66.9f, 0.0f},
		gl::vec2{77.0f,  33.0f}, gl::vec2{ 69.2f, 0.0f},
	};
	drawBezierCurve(dPadCurve);

	drawFilledCircle(tr(centerA), radiusABC, hovered[TRIG_A] || (hoveredRow == TRIG_A));
	drawLetterA(tr(centerA));
	drawFilledCircle(tr(centerB), radiusABC, hovered[TRIG_B] || (hoveredRow == TRIG_B));
	drawLetterB(tr(centerB));
	drawFilledCircle(tr(centerC), radiusABC, hovered[TRIG_C] || (hoveredRow == TRIG_C));
	drawLetterC(tr(centerC));
	drawFilledCircle(tr(centerX), radiusXYZ, hovered[TRIG_X] || (hoveredRow == TRIG_X));
	drawLetterX(tr(centerX));
	drawFilledCircle(tr(centerY), radiusXYZ, hovered[TRIG_Y] || (hoveredRow == TRIG_Y));
	drawLetterY(tr(centerY));
	drawFilledCircle(tr(centerZ), radiusXYZ, hovered[TRIG_Z] || (hoveredRow == TRIG_Z));
	drawLetterZ(tr(centerZ));
	std::array buttonCurve = {
		gl::vec2{221.1f,  28.9f}, gl::vec2{ 80.1f, 0.0f},
		gl::vec2{236.9f, 139.5f}, gl::vec2{-76.8f, 0.0f},
		gl::vec2{221.1f,  28.9f}, gl::vec2{ 80.1f, 0.0f},
	};
	drawBezierCurve(buttonCurve);

	auto corner = (selectBox.bottomRight[1] - selectBox.topLeft[1]) * 0.5;
	auto trR = [&](Rectangle r) { return Rectangle{tr(r.topLeft), tr(r.bottomRight)}; };
	drawFilledRectangle(trR(selectBox), corner, hovered[TRIG_SELECT] || (hoveredRow == TRIG_SELECT));
	drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tr({123.0f, 46.0f}), white, "Select");
	drawFilledRectangle(trR(startBox), corner, hovered[TRIG_START] || (hoveredRow == TRIG_START));
	drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tr({128.0f, 97.0f}), white, "Start");
}

} // namespace joymega

void ImGuiSettings::paintJoystick(MSXMotherBoard& motherBoard)
{
	ImGui::SetNextWindowSize(gl::vec2{316, 323}, ImGuiCond_FirstUseEver);
	im::Window("Configure MSX joysticks", &showConfigureJoystick, [&]{
		ImGui::SetNextItemWidth(13.0f * ImGui::GetFontSize());
		im::Combo("Select joystick", joystickToGuiString(joystick).c_str(), [&]{
			for (const auto& j : xrange(4)) {
				if (ImGui::Selectable(joystickToGuiString(j).c_str())) {
					joystick = j;
				}
			}
		});

		auto& controller = motherBoard.getMSXCommandController();
		auto* setting = dynamic_cast<StringSetting*>(controller.findSetting(settingName(joystick)));
		if (!setting) return;
		auto& interp = setting->getInterpreter();
		TclObject bindings = setting->getValue();

		gl::vec2 scrnPos = ImGui::GetCursorScreenPos();
		gl::vec2 mouse = gl::vec2(ImGui::GetIO().MousePos) - scrnPos;

		// Check if buttons are hovered
		bool msxOrMega = joystick < 2;
		auto hovered = msxOrMega ? msxjoystick::buttonsHovered(mouse)
		                         : joymega    ::buttonsHovered(mouse);
		const auto numButtons = hovered.size();
		using SP = std::span<const zstring_view>;
		auto keyNames = msxOrMega ? SP{msxjoystick::keyNames}
		                          : SP{joymega    ::keyNames};
		auto buttonNames = msxOrMega ? SP{msxjoystick::buttonNames}
		                             : SP{joymega    ::buttonNames};

		// Any joystick button clicked?
		std::optional<int> addAction;
		std::optional<int> removeAction;
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			for (auto i : xrange(numButtons)) {
				if (hovered[i]) addAction = narrow<int>(i);
			}
		}

		ImGui::Dummy(msxOrMega ? msxjoystick::boundingBox : joymega::boundingBox); // reserve space for joystick drawing

		// Draw table
		int hoveredRow = -1;
		const auto& style = ImGui::GetStyle();
		auto textHeight = ImGui::GetTextLineHeight();
		float rowHeight = 2.0f * style.FramePadding.y + textHeight;
		float tableHeight = int(numButtons) * (rowHeight + 2.0f * style.CellPadding.y);
		im::Table("##joystick-table", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX, {0.0f, tableHeight}, [&]{
			im::ID_for_range(numButtons, [&](int i) {
				TclObject key(keyNames[i]);
				TclObject bindingList = bindings.getDictValue(interp, key);
				if (ImGui::TableNextColumn()) {
					auto pos = ImGui::GetCursorPos();
					ImGui::Selectable("##row", hovered[i], ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0, rowHeight));
					if (ImGui::IsItemHovered()) {
						hoveredRow = i;
					}

					ImGui::SetCursorPos(pos);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(buttonNames[i]);
				}
				if (ImGui::TableNextColumn()) {
					if (ImGui::Button("Add")) {
						addAction = i;
					}
					ImGui::SameLine();
					auto numBindings = bindingList.size();
					im::Disabled(numBindings == 0, [&]{
						if (ImGui::Button("Remove")) {
							if (numBindings == 1) {
								bindings.setDictValue(interp, key, TclObject{});
								setting->setValue(bindings);
							} else {
								removeAction = i;
							}
						}
					});
					ImGui::SameLine();
					if (numBindings == 0) {
						ImGui::TextDisabled("no bindings");
					} else {
						size_t lastBindingIndex = numBindings - 1;
						size_t bindingIndex = 0;
						for (auto binding: bindingList) {
							ImGui::TextUnformatted(binding);
							simpleToolTip(toGuiString(*parseBooleanInput(binding)));
							if (bindingIndex < lastBindingIndex) {
								ImGui::SameLine();
								ImGui::TextUnformatted("|"sv);
								ImGui::SameLine();
							}
							++bindingIndex;
						}
					}
				}
			});
		});
		msxOrMega ? msxjoystick::draw(scrnPos, hovered, hoveredRow)
		          : joymega    ::draw(scrnPos, hovered, hoveredRow);

		if (ImGui::Button("Default bindings...")) {
			ImGui::OpenPopup("bindings");
		}
		im::Popup("bindings", [&]{
			auto addOrSet = [&](auto getBindings) {
				if (ImGui::MenuItem("Add to current bindings")) {
					// merge 'newBindings' into 'bindings'
					auto newBindings = getBindings();
					for (auto k : xrange(int(numButtons))) {
						TclObject key(keyNames[k]);
						TclObject dstList = bindings.getDictValue(interp, key);
						TclObject srcList = newBindings.getDictValue(interp, key);
						// This is O(N^2), but that's fine (here).
						for (auto b : srcList) {
							if (!contains(dstList, b)) {
								dstList.addListElement(b);
							}
						}
						bindings.setDictValue(interp, key, dstList);
					}
					setting->setValue(bindings);
				}
				if (ImGui::MenuItem("Replace current bindings")) {
					setting->setValue(getBindings());
				}
			};
			im::Menu("Keyboard", [&]{
				addOrSet([] {
					return TclObject(TclObject::MakeDictTag{},
						"UP",    makeTclList("keyb Up"),
						"DOWN",  makeTclList("keyb Down"),
						"LEFT",  makeTclList("keyb Left"),
						"RIGHT", makeTclList("keyb Right"),
						"A",     makeTclList("keyb Space"),
						"B",     makeTclList("keyb M"));
				});
			});
			for (auto i : xrange(SDL_NumJoysticks())) {
				im::Menu(SDL_JoystickNameForIndex(i), [&]{
					addOrSet([&]{
						return msxOrMega
							? MSXJoystick::getDefaultConfig(i + 1)
							: JoyMega::getDefaultConfig(i + 1);
					});
				});
			}
		});

		// Popup for 'Add'
		static constexpr auto addTitle = "Waiting for input";
		if (addAction) {
			popupForKey = *addAction;
			popupTimeout = 5.0f;
			initListener();
			ImGui::OpenPopup(addTitle);
		}
		im::PopupModal(addTitle, nullptr, ImGuiWindowFlags_NoSavedSettings, [&]{
			auto close = [&]{
				ImGui::CloseCurrentPopup();
				popupForKey = unsigned(-1);
				deinitListener();
			};
			if (popupForKey >= numButtons) {
				close();
				return;
			}

			ImGui::Text("Enter event for joystick button '%s'", buttonNames[popupForKey].c_str());
			ImGui::Text("Or press ESC to cancel.  Timeout in %d seconds.", int(popupTimeout));

			popupTimeout -= ImGui::GetIO().DeltaTime;
			if (popupTimeout <= 0.0f) {
				close();
			}
		});

		// Popup for 'Remove'
		if (removeAction) {
			popupForKey = *removeAction;
			ImGui::OpenPopup("remove");
		}
		im::Popup("remove", [&]{
			auto close = [&]{
				ImGui::CloseCurrentPopup();
				popupForKey = unsigned(-1);
			};
			if (popupForKey >= numButtons) {
				close();
				return;
			}
			TclObject key(keyNames[popupForKey]);
			TclObject bindingList = bindings.getDictValue(interp, key);

			unsigned remove = unsigned(-1);
			unsigned counter = 0;
			for (const auto& b : bindingList) {
				if (ImGui::Selectable(b.c_str())) {
					remove = counter;
				}
				simpleToolTip(toGuiString(*parseBooleanInput(b)));
				++counter;
			}
			if (remove != unsigned(-1)) {
				bindingList.removeListIndex(interp, remove);
				bindings.setDictValue(interp, key, bindingList);
				setting->setValue(bindings);
				close();
			}

			if (ImGui::Selectable("all bindings")) {
				bindings.setDictValue(interp, key, TclObject{});
				setting->setValue(bindings);
				close();
			}
		});
	});
}

void ImGuiSettings::paint(MSXMotherBoard* motherBoard)
{
	if (motherBoard && showConfigureJoystick) paintJoystick(*motherBoard);
}

int ImGuiSettings::signalEvent(const Event& event)
{
	bool msxOrMega = joystick < 2;
	using SP = std::span<const zstring_view>;
	auto keyNames = msxOrMega ? SP{msxjoystick::keyNames}
	                          : SP{joymega    ::keyNames};
	const auto numButtons = keyNames.size();

	if (popupForKey >= numButtons) {
		deinitListener();
		return 0;
	}

	bool escape = false;
	if (const auto* keyDown = get_event_if<KeyDownEvent>(event)) {
		escape = keyDown->getKeyCode() == SDLK_ESCAPE;
	}
	if (!escape) {
		auto getJoyDeadZone = [&](int joyNum) {
			auto& settings = manager.getReactor().getGlobalSettings();
			return settings.getJoyDeadZoneSetting(joyNum).getInt();
		};
		auto b = captureBooleanInput(event, getJoyDeadZone);
		if (!b) return EventDistributor::HOTKEY; // keep popup active
		auto bs = toString(*b);

		auto* motherBoard = manager.getReactor().getMotherBoard();
		if (!motherBoard) return EventDistributor::HOTKEY;
		auto& controller = motherBoard->getMSXCommandController();
		auto* setting = dynamic_cast<StringSetting*>(controller.findSetting(settingName(joystick)));
		if (!setting) return EventDistributor::HOTKEY;
		auto& interp = setting->getInterpreter();

		TclObject bindings = setting->getValue();
		TclObject key(keyNames[popupForKey]);
		TclObject bindingList = bindings.getDictValue(interp, key);

		if (!contains(bindingList, bs)) {
			bindingList.addListElement(bs);
			bindings.setDictValue(interp, key, bindingList);
			setting->setValue(bindings);
		}
	}

	popupForKey = unsigned(-1); // close popup
	return EventDistributor::HOTKEY; // block event
}

void ImGuiSettings::initListener()
{
	if (listening) return;
	listening = true;

	auto& distributor = manager.getReactor().getEventDistributor();
	// highest priority (higher than HOTKEY and IMGUI)
	distributor.registerEventListener(EventType::KEY_DOWN, *this);
	distributor.registerEventListener(EventType::MOUSE_BUTTON_DOWN, *this);
	distributor.registerEventListener(EventType::JOY_BUTTON_DOWN, *this);
	distributor.registerEventListener(EventType::JOY_HAT, *this);
	distributor.registerEventListener(EventType::JOY_AXIS_MOTION, *this);
}

void ImGuiSettings::deinitListener()
{
	if (!listening) return;
	listening = false;

	auto& distributor = manager.getReactor().getEventDistributor();
	distributor.unregisterEventListener(EventType::JOY_AXIS_MOTION, *this);
	distributor.unregisterEventListener(EventType::JOY_HAT, *this);
	distributor.unregisterEventListener(EventType::JOY_BUTTON_DOWN, *this);
	distributor.unregisterEventListener(EventType::MOUSE_BUTTON_DOWN, *this);
	distributor.unregisterEventListener(EventType::KEY_DOWN, *this);
}

} // namespace openmsx
