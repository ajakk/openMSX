#include "ImGuiBitmapViewer.hh"

#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiPalette.hh"
#include "ImGuiUtils.hh"

#include "DisplayMode.hh"
#include "StringOp.hh"
#include "VDP.hh"
#include "VDPVRAM.hh"

#include "ranges.hh"

#include <imgui.h>


namespace openmsx {

ImGuiBitmapViewer::ImGuiBitmapViewer(ImGuiManager& manager_)
	: manager(manager_)
{
}

void ImGuiBitmapViewer::save(ImGuiTextBuffer& buf)
{
	savePersistent(buf, *this, persistentElements);
}

void ImGuiBitmapViewer::loadLine(std::string_view name, zstring_view value)
{
	loadOnePersistent(name, value, *this, persistentElements);
}

void ImGuiBitmapViewer::paint(MSXMotherBoard* motherBoard)
{
	if (!showBitmapViewer) return;
	if (!motherBoard) return;

	ImGui::SetNextWindowSize({532, 562}, ImGuiCond_FirstUseEver);
	im::Window("Bitmap viewer", &showBitmapViewer, [&]{
		VDP* vdp = dynamic_cast<VDP*>(motherBoard->findDevice("VDP")); // TODO name based OK?
		if (!vdp) return;

		auto parseMode = [](DisplayMode mode) {
			auto base = mode.getBase();
			if (base == DisplayMode::GRAPHIC4) return SCR5;
			if (base == DisplayMode::GRAPHIC5) return SCR6;
			if (base == DisplayMode::GRAPHIC6) return SCR7;
			if (base != DisplayMode::GRAPHIC7) return OTHER;
			if (mode.getByte() & DisplayMode::YJK) {
				if (mode.getByte() & DisplayMode::YAE) {
					return SCR11;
				} else {
					return SCR12;
				}
			} else {
				return SCR8;
			}
		};
		int vdpMode = parseMode(vdp->getDisplayMode());

		int vdpPages = vdpMode <= SCR6 ? 4 : 2;
		int vdpPage = vdp->getDisplayPage();
		if (vdpPage >= vdpPages) vdpPage &= 1;

		int vdpLines = (vdp->getNumberOfLines() == 192) ? 0 : 1;

		int vdpColor0 = [&]{
			if (vdpMode == one_of(SCR8, SCR11, SCR12) || !vdp->getTransparency()) {
				return 16; // no replacement
			}
			return vdp->getBackgroundColor() & 15;
		}();

		auto modeToStr = [](int mode) {
			if (mode == SCR5 ) return "screen 5";
			if (mode == SCR6 ) return "screen 6";
			if (mode == SCR7 ) return "screen 7";
			if (mode == SCR8 ) return "screen 8";
			if (mode == SCR11) return "screen 11";
			if (mode == SCR12) return "screen 12";
			if (mode == OTHER) return "non-bitmap";
			assert(false); return "ERROR";
		};

		static const char* const color0Str = "0\0001\0002\0003\0004\0005\0006\0007\0008\0009\00010\00011\00012\00013\00014\00015\000none\000";
		im::Group([&]{
			ImGui::RadioButton("Use VDP settings", &bitmapManual, 0);
			im::Disabled(bitmapManual != 0, [&]{
				ImGui::AlignTextToFramePadding();
				ImGui::StrCat("Screen mode: ", modeToStr(vdpMode));
				ImGui::AlignTextToFramePadding();
				ImGui::StrCat("Display page: ", vdpPage);
				ImGui::AlignTextToFramePadding();
				ImGui::StrCat("Visible lines: ", vdpLines ? 212 : 192);
				ImGui::AlignTextToFramePadding();
				ImGui::StrCat("Replace color 0: ", getComboString(vdpColor0, color0Str));
				ImGui::AlignTextToFramePadding();
				ImGui::StrCat("Interlace: ", "TODO");
			});
		});
		ImGui::SameLine();
		im::Group([&]{
			ImGui::RadioButton("Manual override", &bitmapManual, 1);
			im::Disabled(bitmapManual != 1, [&]{
				im::ItemWidth(ImGui::GetFontSize() * 9.0f, [&]{
					ImGui::Combo("##Screen mode", &bitmapScrnMode, "screen 5\000screen 6\000screen 7\000screen 8\000screen 11\000screen 12\000");
					int numPages = bitmapScrnMode <= SCR6 ? 4 : 2; // TODO extended VRAM
					if (bitmapPage >= numPages) bitmapPage = numPages - 1;
					ImGui::Combo("##Display page", &bitmapPage, numPages == 2 ? "0\0001\000" : "0\0001\0002\0003\000");
					ImGui::Combo("##Visible lines", &bitmapLines, "192\000212\000256\000");
					ImGui::Combo("##Color 0 replacement", &bitmapColor0, color0Str);
				});
			});
		});

		ImGui::SameLine();
		ImGui::Dummy(ImVec2(25, 1));
		ImGui::SameLine();
		im::Group([&]{
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
			ImGui::Combo("Palette", &manager.palette.whichPalette, "VDP\000Custom\000Fixed\000");
			if (ImGui::Button("Open palette editor")) {
				manager.palette.show = true;
			}
			ImGui::Separator();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.0f);
			ImGui::Combo("Zoom", &bitmapZoom, "1x\0002x\0003x\0004x\0005x\0006x\0007x\0008x\000");
			ImGui::Checkbox("grid", &bitmapGrid);
			ImGui::SameLine();
			im::Disabled(!bitmapGrid, [&]{
				ImGui::ColorEdit4("Grid color", &bitmapGridColor[0],
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar);
			});
		});

		ImGui::Separator();

		auto& vram = vdp->getVRAM();
		int mode   = bitmapManual ? bitmapScrnMode : vdpMode;
		int page   = bitmapManual ? bitmapPage     : vdpPage;
		int lines  = bitmapManual ? bitmapLines    : vdpLines;
		int color0 = bitmapManual ? bitmapColor0   : vdpColor0;
		int width  = mode == one_of(SCR6, SCR7) ? 512 : 256;
		int height = (lines == 0) ? 192
				: (lines == 1) ? 212
						: 256;

		std::array<uint32_t, 16> palette;
		auto msxPalette = manager.palette.getPalette(vdp);
		ranges::transform(msxPalette, palette.data(),
			[](uint16_t msx) { return ImGuiPalette::toRGBA(msx); });
		if (color0 < 16) palette[0] = palette[color0];

		std::array<uint32_t, 512 * 256> pixels;
		renderBitmap(vram.getData(), palette, mode, height, page,
				pixels.data());
		if (!bitmapTex) {
			bitmapTex.emplace(false, false); // no interpolation, no wrapping
		}
		bitmapTex->bind();
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
				GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
		int zx = (1 + bitmapZoom) * (width == 256 ? 2 : 1);
		int zy = (1 + bitmapZoom) * 2;

		im::Child("##bitmap", ImGui::GetContentRegionAvail(), false, ImGuiWindowFlags_HorizontalScrollbar, [&]{
			auto pos = ImGui::GetCursorPos();
			ImVec2 size(float(width * zx), float(height * zy));
			ImGui::Image(reinterpret_cast<void*>(bitmapTex->get()), size);

			if (bitmapGrid && (zx > 1) && (zy > 1)) {
				auto color = ImGui::ColorConvertFloat4ToU32(bitmapGridColor);
				for (auto y : xrange(zy)) {
					auto* line = &pixels[y * zx];
					for (auto x : xrange(zx)) {
						line[x] = (x == 0 || y == 0) ? color : 0;
					}
				}
				if (!bitmapGridTex) {
					bitmapGridTex.emplace(false, true); // no interpolation, with wrapping
				}
				bitmapGridTex->bind();
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, zx, zy, 0,
						GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
				ImGui::SetCursorPos(pos);
				ImGui::Image(reinterpret_cast<void*>(bitmapGridTex->get()), size,
						ImVec2(0.0f, 0.0f), ImVec2(float(width), float(height)));
			}
		});
	});
}

// TODO avoid code duplication with src/video/BitmapConverter
void ImGuiBitmapViewer::renderBitmap(std::span<const uint8_t> vram, std::span<const uint32_t, 16> palette16,
                                     int mode, int lines, int page, uint32_t* output)
{
	auto yjk2rgb = [](int y, int j, int k) -> std::tuple<int, int, int> {
		// Note the formula for 'blue' differs from the 'traditional' formula
		// (e.g. as specified in the V9958 datasheet) in the rounding behavior.
		// Confirmed on real turbor machine. For details see:
		//    https://github.com/openMSX/openMSX/issues/1394
		//    https://twitter.com/mdpc___/status/1480432007180341251?s=20
		int r = std::clamp(y + j,                       0, 31);
		int g = std::clamp(y + k,                       0, 31);
		int b = std::clamp((5 * y - 2 * j - k + 2) / 4, 0, 31);
		return {r, g, b};
	};

	// TODO handle less than 128kB VRAM (will crash now)
	size_t addr = 0x8000 * page;
	switch (mode) {
	case SCR5:
		for (auto y : xrange(lines)) {
			auto* line = &output[256 * y];
			for (auto x : xrange(128)) {
				auto value = vram[addr];
				line[2 * x + 0] = palette16[(value >> 4) & 0x0f];
				line[2 * x + 1] = palette16[(value >> 0) & 0x0f];
				++addr;
			}
		}
		break;

	case SCR6:
		for (auto y : xrange(lines)) {
			auto* line = &output[512 * y];
			for (auto x : xrange(128)) {
				auto value = vram[addr];
				line[4 * x + 0] = palette16[(value >> 6) & 3];
				line[4 * x + 1] = palette16[(value >> 4) & 3];
				line[4 * x + 2] = palette16[(value >> 2) & 3];
				line[4 * x + 3] = palette16[(value >> 0) & 3];
				++addr;
			}
		}
		break;

	case SCR7:
		for (auto y : xrange(lines)) {
			auto* line = &output[512 * y];
			for (auto x : xrange(128)) {
				auto value0 = vram[addr + 0x00000];
				auto value1 = vram[addr + 0x10000];
				line[4 * x + 0] = palette16[(value0 >> 4) & 0x0f];
				line[4 * x + 1] = palette16[(value0 >> 0) & 0x0f];
				line[4 * x + 2] = palette16[(value1 >> 4) & 0x0f];
				line[4 * x + 3] = palette16[(value1 >> 0) & 0x0f];
				++addr;
			}
		}
		break;

	case SCR8: {
		auto toColor = [](uint8_t value) {
			int r = (value & 0x1c) >> 2;
			int g = (value & 0xe0) >> 5;
			int b = (value & 0x03) >> 0;
			int rr = (r << 5) | (r << 2) | (r >> 1);
			int gg = (g << 5) | (g << 2) | (g >> 1);
			int bb = (b << 6) | (b << 4) | (b << 2) | (b << 0);
			int aa = 255;
			return (rr << 0) | (gg << 8) | (bb << 16) | (aa << 24);
		};
		for (auto y : xrange(lines)) {
			auto* line = &output[256 * y];
			for (auto x : xrange(128)) {
				line[2 * x + 0] = toColor(vram[addr + 0x00000]);
				line[2 * x + 1] = toColor(vram[addr + 0x10000]);
				++addr;
			}
		}
		break;
	}

	case SCR11:
		for (auto y : xrange(lines)) {
			auto* line = &output[256 * y];
			for (auto x : xrange(64)) {
				std::array<unsigned, 4> p = {
					vram[addr + 0 + 0x00000],
					vram[addr + 0 + 0x10000],
					vram[addr + 1 + 0x00000],
					vram[addr + 1 + 0x10000],
				};
				addr += 2;
				int j = narrow<int>((p[2] & 7) + ((p[3] & 3) << 3)) - narrow<int>((p[3] & 4) << 3);
				int k = narrow<int>((p[0] & 7) + ((p[1] & 3) << 3)) - narrow<int>((p[1] & 4) << 3);
				for (auto n : xrange(4)) {
					uint32_t pix;
					if (p[n] & 0x08) {
						pix = palette16[p[n] >> 4];
					} else {
						int Y = narrow<int>(p[n] >> 3);
						auto [r, g, b] = yjk2rgb(Y, j, k);
						int rr = (r << 3) | (r >> 2);
						int gg = (g << 3) | (g >> 2);
						int bb = (b << 3) | (b >> 2);
						int aa = 255;
						pix = (rr << 0) | (gg << 8) | (bb << 16) | (aa << 24);
					}
					line[4 * x + n] = pix;
				}
			}
		}
		break;

	case SCR12:
		for (auto y : xrange(lines)) {
			auto* line = &output[256 * y];
			for (auto x : xrange(64)) {
				std::array<unsigned, 4> p = {
					vram[addr + 0 + 0x00000],
					vram[addr + 0 + 0x10000],
					vram[addr + 1 + 0x00000],
					vram[addr + 1 + 0x10000],
				};
				addr += 2;
				int j = narrow<int>((p[2] & 7) + ((p[3] & 3) << 3)) - narrow<int>((p[3] & 4) << 3);
				int k = narrow<int>((p[0] & 7) + ((p[1] & 3) << 3)) - narrow<int>((p[1] & 4) << 3);
				for (auto n : xrange(4)) {
					int Y = narrow<int>(p[n] >> 3);
					auto [r, g, b] = yjk2rgb(Y, j, k);
					int rr = (r << 3) | (r >> 2);
					int gg = (g << 3) | (g >> 2);
					int bb = (b << 3) | (b >> 2);
					int aa = 255;
					line[4 * x + n] = (rr << 0) | (gg << 8) | (bb << 16) | (aa << 24);
				}
			}
		}
		break;

	case OTHER:
		for (auto y : xrange(lines)) {
			auto* line = &output[256 * y];
			for (auto x : xrange(256)) {
				line[x] = 0xFF808080; // gray
			}
		}
		break;
	}
}

} // namespace openmsx
