#pragma once

#include "OFS_Videoplayer.h"
#include "GradientBar.h"
#include "OFS_Videopreview.h"
#include "FunscriptHeatmap.h"

#include <functional>

// ImDrawList* draw_list, const ImRect& frame_bb, bool item_hovered
using TimelineCustomDrawFunc = std::function<void(ImDrawList*, const ImRect&, bool)>;

class OFS_VideoplayerControls
{
private:
	float actualPlaybackSpeed = 1.f;
	float lastPlayerPosition = 0.0f;

	uint32_t measureStartTime = 0;
	bool mute = false;
	bool hasSeeked = false;
	bool dragging = false;
	
	static constexpr int32_t PreviewUpdateMs = 1000;
	uint32_t lastPreviewUpdate = 0;

	void VideoLoaded(SDL_Event& ev) noexcept;
public:
	static constexpr const char* ControlId = "###CONTROLS";
	static constexpr const char* TimeId = "###TIME";
	VideoplayerWindow* player = nullptr;
	HeatmapGradient Heatmap;
	std::unique_ptr<VideoPreview> videoPreview;

	OFS_VideoplayerControls() noexcept {}
	void setup() noexcept;
	inline void Destroy() noexcept { videoPreview.reset(); }

	inline void UpdateHeatmap(float totalDuration, const FunscriptArray& actions) noexcept
	{
		Heatmap.Update(totalDuration, actions);
	}

	bool DrawTimelineWidget(const char* label, float* position, TimelineCustomDrawFunc&& customDraw) noexcept;

	void DrawTimeline(bool* open, TimelineCustomDrawFunc&& customDraw = [](ImDrawList*, const ImRect&, bool) {}) noexcept;
	void DrawControls(bool* open) noexcept;
};