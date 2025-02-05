#ifndef IMGUI_BITMAP_VIEWER_HH
#define IMGUI_BITMAP_VIEWER_HH

#include "ImGuiPart.hh"

#include "GLUtil.hh"
#include "gl_vec.hh"

#include <cstdint>
#include <optional>
#include <span>

namespace openmsx {

class ImGuiManager;
class MSXMotherBoard;

class ImGuiBitmapViewer final : public ImGuiPart
{
public:
	ImGuiBitmapViewer(ImGuiManager& manager);

	[[nodiscard]] zstring_view iniName() const override { return "bitmap viewer"; }
	void save(ImGuiTextBuffer& buf) override;
	void loadLine(std::string_view name, zstring_view value) override;
	void paint(MSXMotherBoard* motherBoard) override;

private:
	void renderBitmap(std::span<const uint8_t> vram, std::span<const uint32_t, 16> palette16,
	                  int mode, int lines, int page, uint32_t* output);

public:
	bool showBitmapViewer = false;

private:
	ImGuiManager& manager;

	int bitmapManual = 0; // 0 -> use VDP settings, 1 -> use manual settings
	enum BitmapScrnMode : int { SCR5, SCR6, SCR7, SCR8, SCR11, SCR12, OTHER };
	int bitmapScrnMode = 0;
	int bitmapPage = 0; // 0-3 or 0-1 depending on screen mode   TODO extended VRAM
	int bitmapLines = 1; // 0->192, 1->212, 2->256
	int bitmapColor0 = 16; // 0..15, 16->no replacement
	int bitmapZoom = 0; // 0->1x, 1->2x, ..., 7->8x
	bool bitmapGrid = true;
	gl::vec4 bitmapGridColor{0.0f, 0.0f, 0.0f, 0.5f}; // RGBA

	std::optional<gl::Texture> bitmapTex; // TODO also deallocate when needed
	std::optional<gl::Texture> bitmapGridTex;

	static constexpr auto persistentElements = std::tuple{
		PersistentElement   {"show",     &ImGuiBitmapViewer::showBitmapViewer},
		PersistentElementMax{"override", &ImGuiBitmapViewer::bitmapManual, 2},
		PersistentElementMax{"scrnMode", &ImGuiBitmapViewer::bitmapScrnMode, OTHER}, // SCR5..SCR12
		PersistentElementMax{"page",     &ImGuiBitmapViewer::bitmapPage, 4},
		PersistentElementMax{"lines",    &ImGuiBitmapViewer::bitmapLines, 3},
		PersistentElementMax{"color0",   &ImGuiBitmapViewer::bitmapColor0, 16 + 1},
		PersistentElementMax{"zoom",     &ImGuiBitmapViewer::bitmapZoom, 8},
		PersistentElement   {"showGrid", &ImGuiBitmapViewer::bitmapGrid},
		PersistentElement   {"gridColor",&ImGuiBitmapViewer::bitmapGridColor}
	};
};

} // namespace openmsx

#endif
