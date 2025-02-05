#ifndef IMGUI_MEDIA_HH
#define IMGUI_MEDIA_HH

#include "ImGuiPart.hh"

#include "CartridgeSlotManager.hh"
#include "HD.hh"
#include "IDECDROM.hh"
#include "RealDrive.hh"
#include "RomTypes.hh"
#include "TclObject.hh"

#include "circular_buffer.hh"
#include "zstring_view.hh"

#include <array>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace openmsx {

class HardwareConfig;
class ImGuiManager;
class MSXMotherBoard;
class RomInfo;

class ImGuiMedia final : public ImGuiPart
{
public:
	struct ExtensionInfo {
		std::string configName;
		std::string displayName;
		std::vector<std::pair<std::string, std::string>> configInfo;
		std::optional<std::string> testResult; // lazily initialized
	};

public:
	ImGuiMedia(ImGuiManager& manager_)
		: manager(manager_) {}

	[[nodiscard]] zstring_view iniName() const override { return "media"; }
	void save(ImGuiTextBuffer& buf) override;
	void loadLine(std::string_view name, zstring_view value) override;
	void showMenu(MSXMotherBoard* motherBoard) override;
	void paint(MSXMotherBoard* motherBoard) override;

	[[nodiscard]] std::string displayNameForExtension(std::string_view config);
	[[nodiscard]] std::string displayNameForRom(const std::string& filename, bool compact = false);
	[[nodiscard]] std::string displayNameForHardwareConfig(const HardwareConfig& config, bool compact = false);
	[[nodiscard]] std::string displayNameForSlotContent(const CartridgeSlotManager& slotManager, unsigned slotNr, bool compact = false);
	[[nodiscard]] std::string slotAndNameForHardwareConfig(const CartridgeSlotManager& slotManager, const HardwareConfig& config);
	[[nodiscard]] std::string displayNameForDriveContent(unsigned drive, bool compact = false);

	std::vector<ExtensionInfo>& getAllExtensions();
	ExtensionInfo* findExtensionInfo(std::string_view config);
	[[nodiscard]] const std::string& getTestResult(ExtensionInfo& info);

public:
	enum SelectDiskType {
		SELECT_DISK_IMAGE,
		SELECT_DIR_AS_DISK,
		SELECT_RAMDISK,
		SELECT_EMPTY_DISK,
	};
	enum SelectCartridgeType {
		SELECT_ROM_IMAGE,
		SELECT_EXTENSION,
		SELECT_EMPTY_SLOT,
	};

	struct MediaItem {
		std::string name;
		std::vector<std::string> ipsPatches; // only used for disk and rom images
		RomType romType = ROM_UNKNOWN; // only used for rom images

		[[nodiscard]] bool operator==(const MediaItem&) const = default;
	};

	struct ItemGroup { // MediaItem + history
		static constexpr size_t HISTORY_SIZE = 8;

		MediaItem edit;
		int patchIndex = -1; // only used for IPSItem
		circular_buffer<MediaItem> recent{HISTORY_SIZE};
	};

	struct CartridgeMediaInfo {
		std::array<ItemGroup, 2> groups;
		int select = 0; // 0-> romImage, 1->extension
		bool show = false;
	};
	struct DiskMediaInfo {
		DiskMediaInfo() {
			groups[2].edit.name = "ramdsk";
		}
		std::array<ItemGroup, 3> groups;
		int select = 0; // 0->diskImage, 1->dirAsDsk, 2->ramDisk
		bool show = false;
	};
	struct CassetteMediaInfo {
		ItemGroup group;
		bool show = false;
	};

public:
	bool resetOnInsertRom = true;

	static void printDatabase(const RomInfo& romInfo, const char* buf);
	static bool selectMapperType(const char* label, RomType& item);

	static std::string diskFilter();

private:
	bool selectRecent(ItemGroup& group, std::function<std::string(const std::string&)> displayFunc, float width);
	bool selectImage(ItemGroup& group, const std::string& title,
	                 std::function<std::string()> createFilter, zstring_view current,
	                 std::function<std::string(const std::string&)> displayFunc = std::identity{},
	                 std::function<void()> createNewCallback = {});
	bool selectDirectory(ItemGroup& info, const std::string& title, zstring_view current,
	                     std::function<void()> createNewCallback);
	bool selectPatches(MediaItem& item, int& patchIndex);
	bool insertMediaButton(std::string_view mediaName, ItemGroup& group, bool* showWindow);
	TclObject showDiskInfo(std::string_view mediaName, DiskMediaInfo& info);
	TclObject showCartridgeInfo(std::string_view mediaName, CartridgeMediaInfo& info, int slot);
	void diskMenu(int i);
	void cartridgeMenu(int i);
	void cassetteMenu(const TclObject& cmdResult);
	void insertMedia(std::string_view mediaName, ItemGroup& group);

	void printExtensionInfo(ExtensionInfo& info);
	void extensionTooltip(ExtensionInfo& info);
	bool drawExtensionFilter();

private:
	ImGuiManager& manager;

	std::array<DiskMediaInfo, RealDrive::MAX_DRIVES> diskMediaInfo;
	std::array<CartridgeMediaInfo, CartridgeSlotManager::MAX_SLOTS> cartridgeMediaInfo;
	ItemGroup extensionMediaInfo;
	CassetteMediaInfo cassetteMediaInfo;
	std::array<ItemGroup, HD::MAX_HD> hdMediaInfo;
	std::array<ItemGroup, IDECDROM::MAX_CD> cdMediaInfo;
	ItemGroup laserdiscMediaInfo;

	std::string filterType;
	std::string filterString;
	bool filterOpen = false;

	std::vector<ExtensionInfo> extensionInfo;

	static constexpr auto persistentElements = std::tuple{
		PersistentElement{"resetOnInsertRom", &ImGuiMedia::resetOnInsertRom},
		// most media stuff is handled elsewhere
	};
};

} // namespace openmsx

#endif
