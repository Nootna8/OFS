#pragma once

#include "OpenFunscripter.h"
#include "OFS_ScriptingMode.h"
#include "ScriptPositionsOverlayMode.h"

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "SDL_events.h"

#include "Model/TrackingTarget.h"
#include "Model/EventList.h"

#include <cmath> 
#include <vector>
#include <functional>

enum UtilState
{
	SS_NONE,
	SS_STARTED,
	SS_P1_SELECTED,
	SS_P2_SELECTED
};

class RectUtil
{
public:
	RectUtil();
	
	void SelectRect();

	void mousePressed(SDL_Event& ev) noexcept;
	void mouseReleased(SDL_Event& ev) noexcept;
	void mouseDrag(SDL_Event& ev) noexcept;
	
	void Draw() noexcept;
	void Reset();
	void DrawRect(cv::Rect2f, ImColor color);
	
protected:
	ImVec2 MousePosition();
	bool DrawButton(ImRect r, const char* id, cv::Scalar color);
	void DrawTargetCorners(jt::TargetPtr target);

	ImGuiID pressedId;
	ImRect lastRect;
	ImVec2 pressedAt;

	UtilState state = SS_NONE;
	std::function<ImVec2(ImVec2)> mouseTranslation;
};

class JTTrackingMode : public ScripingModeBaseImpl
{
public:
	JTTrackingMode();

	virtual void DrawModeSettings() noexcept override;
	virtual void addEditAction(FunscriptAction action) noexcept override;
	void StartTracking();
	void StopTracking();
	void DrawVideoOverlay();
	void VideoRenderUpdate(SDL_Event& ev) noexcept;

	virtual void undo() noexcept override;
	virtual void redo() noexcept override;
	virtual void update() noexcept override;
protected:
	void DrawPerformance();
	void MoveSimulator();
	void WritePositions();

	float renderFrame = 0;
	std::vector<std::deque<int>> threadLog;
	std::deque<int> jobLog;

	bool moveSimulator = false;
	bool showPerformance = false;
	bool showMovement = true;
	std::shared_ptr<jt::TrackingSet> set;
	std::unique_ptr<jt::TrackingRunner> runner;
	cv::Ptr<jt::VideoReader> reader;
	RectUtil rectUtil;
};

class ScriptTrackingOverlay : public FrameOverlay
{
public:
	ScriptTrackingOverlay(class ScriptTimeline* timeline);

	virtual void DrawSettings() noexcept override;
	virtual void DrawScriptPositionContent(const OverlayDrawingCtx& ctx) noexcept override;
	virtual void DrawActionLines(const OverlayDrawingCtx& ctx, jt::EventList& events) noexcept;
	void DrawBookmarks(const OverlayDrawingCtx& ctx) noexcept;
	void DrawEvents(const OverlayDrawingCtx& ctx) noexcept;
	void OnSelection(SDL_Event& ev) noexcept;
	bool HandleDelete() noexcept override;
protected:
	OFS_ScriptSettings::Bookmark* selectedBookmark = nullptr;
	bool showAllModes = false;
	time_t selectFrom = 0;
	time_t selectTo = 0;
};