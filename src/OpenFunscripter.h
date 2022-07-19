#pragma once
#include "OFS_ScriptingMode.h"
#include "KeybindingSystem.h"
#include "OFS_Settings.h"
#include "OFS_ScriptTimeline.h"
#include "OFS_Videoplayer.h"
#include "OFS_UndoSystem.h"
#include "EventSystem.h"
#include "OFS_ScriptSimulator.h"
#include "OFS_ControllerInput.h"
#include "GradientBar.h"
#include "OFS_SpecialFunctions.h"
#include "OFS_Events.h"
#include "OFS_VideoplayerControls.h"
#include "OFS_TCode.h"
#include "OFS_Project.h"
#include "OFS_AsyncIO.h"
#include "OFS_Simulator3D.h"
#include "OFS_BlockingTask.h"
#include "OFS_DynamicFontAtlas.h"
#include "OFS_LuaExtensions.h"
#include "OFS_Localization.h"

#include <memory>
#include <chrono>

enum OFS_Status : uint8_t
{
	OFS_None = 0x0,
	OFS_ShouldExit = 0x1,
	OFS_Fullscreen = 0x1 << 1,
	OFS_GradientNeedsUpdate = 0x1 << 2,
	OFS_GamepadSetPlaybackSpeed = 0x1 << 3,
	OFS_AutoBackup = 0x1 << 4
};

class OpenFunscripter {
private:
	SDL_Window* window;
	SDL_GLContext glContext;

	bool ShowMetadataEditor = false;
	bool ShowProjectEditor = false;
#ifndef NDEBUG
	bool DebugDemo = false;
#endif
	bool DebugMetrics = false;
	bool ShowAbout = false;
	
	FunscriptArray CopiedSelection;
	std::chrono::steady_clock::time_point lastBackup;

	char tmpBuf[2][32];
	int32_t ActiveFunscriptIdx = 0;

	void registerBindings();

	void update() noexcept;
	void newFrame() noexcept;
	void render() noexcept;
	void autoBackup() noexcept;

	void exitApp(bool force = false) noexcept;

	bool imguiSetup() noexcept;
	void processEvents() noexcept;

	void FunscriptChanged(SDL_Event& ev) noexcept;

	void DragNDrop(SDL_Event& ev) noexcept;

	void MpvVideoLoaded(SDL_Event& ev) noexcept;
	void MpvPlayPauseChange(SDL_Event& ev) noexcept;

	void ControllerAxisPlaybackSpeed(SDL_Event& ev) noexcept;

	void ScriptTimelineActionClicked(SDL_Event& ev) noexcept;
	void ScriptTimelineDoubleClick(SDL_Event& ev) noexcept;
	void ScriptTimelineSelectTime(SDL_Event& ev) noexcept;
	void ScriptTimelineActiveScriptChanged(SDL_Event& ev) noexcept;

	void selectTopPoints() noexcept;
	void selectMiddlePoints() noexcept;
	void selectBottomPoints() noexcept;

	void cutSelection() noexcept;
	void copySelection() noexcept;
	void pasteSelection() noexcept;
	void pasteSelectionExact() noexcept;
	void equalizeSelection() noexcept;
	void invertSelection() noexcept;
	void isolateAction() noexcept;
	void repeatLastStroke() noexcept;

	void saveProject() noexcept;
	void quickExport() noexcept;
	void exportClips() noexcept;
	bool closeProject(bool closeWithUnsavedChanges) noexcept;
	void pickDifferentMedia() noexcept;


	void saveHeatmap(const char* path, int width, int height);
	void updateTitle() noexcept;

	void removeActionButton();
	void removeAction(FunscriptAction action) noexcept;
	void removeAction() noexcept;
	void addEditAction(int pos) noexcept;

	void saveActiveScriptAs();

	bool openFile(const std::string& file, bool withFailDialog) noexcept;
	bool importFile(const std::string& file) noexcept;
	bool openProject(const std::string& file, bool withFailDialog) noexcept;
	void initProject() noexcept;
	
	void SetFullscreen(bool fullscreen);
	void setupDefaultLayout(bool force) noexcept;

	template<typename OnCloseAction>
	void closeWithoutSavingDialog(OnCloseAction&& action) noexcept;

	// UI
	void CreateDockspace() noexcept;
	void ShowAboutWindow(bool* open) noexcept;
	void ShowStatisticsWindow(bool* open) noexcept;
	void ShowMainMenuBar() noexcept;
	bool ShowMetadataEditorWindow(bool* open) noexcept;
public:
	static OpenFunscripter* ptr;
	static std::array<const char*, 6> SupportedVideoExtensions;
	static std::array<const char*, 4> SupportedAudioExtensions;
	uint8_t Status = OFS_Status::OFS_AutoBackup;

	~OpenFunscripter();

	KeybindingSystem keybinds;
	ScriptTimeline scriptTimeline;
	OFS_VideoplayerControls playerControls;
	ScriptSimulator simulator;
	OFS_BlockingTask blockingTask;
	
	std::unique_ptr<TCodePlayer> tcode;
	std::unique_ptr<VideoplayerWindow> player;
	std::unique_ptr<SpecialFunctionsWindow> specialFunctions;
	std::unique_ptr<ScriptingMode> scripting;
	std::unique_ptr<EventSystem> events;
	std::unique_ptr<ControllerInput> controllerInput;
	std::unique_ptr<OFS_Settings> settings;
	std::unique_ptr<UndoSystem> undoSystem;
	std::unique_ptr<OFS_LuaExtensions> extensions;

	std::unique_ptr<Simulator3D> sim3D;

	std::unique_ptr<OFS_Project> LoadedProject;
	std::unique_ptr<OFS_AsyncIO> IO;

	bool setup(int argc, char* argv[]);
	int run() noexcept;
	void step() noexcept;
	void shutdown() noexcept;

	inline const std::vector<std::shared_ptr<Funscript>>& LoadedFunscripts() const noexcept
	{
		return LoadedProject->Funscripts;
	}

	inline bool ScriptLoaded() const { return LoadedFunscripts().size() > 0; }
	inline std::shared_ptr<Funscript>& ActiveFunscript() noexcept {
		FUN_ASSERT(ScriptLoaded(), "No script loaded");
		return LoadedProject->Funscripts[ActiveFunscriptIdx]; 
	}

	inline std::shared_ptr<Funscript>& RootFunscript() noexcept {
		FUN_ASSERT(ScriptLoaded(), "No script loaded");
		// when multiple funscripts are loaded the root funscript will store paths to the associated scripts
		return LoadedProject->Funscripts[0];
	}

	void UpdateNewActiveScript(int32_t activeIndex) noexcept;
	inline int32_t ActiveFunscriptIndex() const { return ActiveFunscriptIdx; }

	static inline Funscript& script() noexcept { return *OpenFunscripter::ptr->ActiveFunscript(); }
	static void SetCursorType(ImGuiMouseCursor id) noexcept;

	inline const FunscriptArray& FunscriptClipboard() const { return CopiedSelection; }

	inline void LoadOverrideFont(const std::string& font) noexcept { 
		OFS_DynFontAtlas::FontOverride = font;
		OFS_DynFontAtlas::ptr->forceRebuild = true;
	}
	void Undo() noexcept;
	void Redo() noexcept;

	bool BookmarkPopup(ImVec2 p1, ImVec2 p2, bool& hover, ImGuiID id, OFS_ScriptSettings::Bookmark* bookmarkStart, OFS_ScriptSettings::Bookmark* bookmarkEnd) noexcept;
	bool BookmarkPopupItems(OFS_ScriptSettings::Bookmark* bookmarkStart, OFS_ScriptSettings::Bookmark* bookmarkEnd) noexcept;
};

template<typename OnCloseAction>
inline void OpenFunscripter::closeWithoutSavingDialog(OnCloseAction&& action) noexcept
{
	if (LoadedProject->HasUnsavedEdits()) {
		Util::YesNoCancelDialog(TR(CLOSE_WITHOUT_SAVING),
			TR(CLOSE_WITHOUT_SAVING_MSG),
			[this, action](Util::YesNoCancel result) {
				if (result == Util::YesNoCancel::Yes) {
					closeProject(true);
					action();
				}
			});
	}
	else {
		action();
	}
}
