#include "OFS_VideoplayerControls.h"
#include "OFS_Util.h"
#include "OFS_Profiling.h"
#include "OFS_ImGui.h"
#include "SDL_timer.h"
static char tmp_buf[2][32];

void OFS_VideoplayerControls::VideoLoaded(SDL_Event& ev) noexcept
{
    OFS_PROFILE(__FUNCTION__);
    if (ev.user.data1 != nullptr) {
        videoPreview->previewVideo((const char*)ev.user.data1, 0.f);
    }
}

void OFS_VideoplayerControls::setup() noexcept
{
    videoPreview = std::make_unique<VideoPreview>();
    videoPreview->setup(false);
    EventSystem::ev().Subscribe(VideoEvents::MpvVideoLoaded, EVENT_SYSTEM_BIND(this, &OFS_VideoplayerControls::VideoLoaded));
}

bool OFS_VideoplayerControls::DrawTimelineWidget(const char* label, float* position, TimelineCustomDrawFunc&& customDraw) noexcept
{
    OFS_PROFILE(__FUNCTION__);
    bool change = false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    auto draw_list = window->DrawList;

    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFontSize() * 1.5f;

    const ImRect frame_bb(window->DC.CursorPos + style.FramePadding, window->DC.CursorPos + ImVec2(w, h) - style.FramePadding);
    const ImRect total_bb(frame_bb.Min, frame_bb.Max);

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id, &frame_bb))
        return false;

    const bool item_hovered = ImGui::IsItemHovered();

    const float current_pos_x = frame_bb.Min.x + frame_bb.GetWidth() * (*position);
    const float offset_progress_h = h / 5.f;
    const float offset_progress_w = current_pos_x - frame_bb.Min.x;
    draw_list->AddRectFilled(
        frame_bb.Min + ImVec2(-1.f, offset_progress_h),
        frame_bb.Min + ImVec2(offset_progress_w, frame_bb.GetHeight()) + ImVec2(0.f, offset_progress_h),
        ImColor(style.Colors[ImGuiCol_PlotLinesHovered]));
    draw_list->AddRectFilled(frame_bb.Min + ImVec2(offset_progress_w, offset_progress_h),
        frame_bb.Max + ImVec2(1.f, offset_progress_h),
        IM_COL32(150, 150, 150, 255));

    customDraw(draw_list, frame_bb, item_hovered);

    // position highlight
    ImVec2 p1(current_pos_x, frame_bb.Min.y);
    ImVec2 p2(current_pos_x, frame_bb.Max.y);
    constexpr float timeline_pos_cursor_w = 4.f;
    draw_list->AddLine(p1 + ImVec2(0.f, h / 3.f), p2 + ImVec2(0.f, h / 3.f), IM_COL32(255, 0, 0, 255), timeline_pos_cursor_w / 2.f);

    // gradient + shadow
    ImGradient::DrawGradientBar(&Heatmap.Gradient, frame_bb.Min, frame_bb.GetWidth(), frame_bb.GetHeight());
    draw_list->AddRectFilledMultiColor(frame_bb.Min, frame_bb.Max, 
        IM_COL32(0, 0, 0, 255),
        IM_COL32(0, 0, 0, 255),
        IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 0));

    const ImColor timeline_cursor_back = IM_COL32(255, 255, 255, 255);
    const ImColor timeline_cursor_front = IM_COL32(0, 0, 0, 255);
    auto mouse = ImGui::GetMousePos();
    float relTimelinePos = ((mouse.x - frame_bb.Min.x) / frame_bb.GetWidth());

    if (item_hovered) {
        draw_list->AddLine(ImVec2(mouse.x, frame_bb.Min.y),
            ImVec2(mouse.x, frame_bb.Max.y),
            timeline_cursor_back, timeline_pos_cursor_w);
        draw_list->AddLine(ImVec2(mouse.x, frame_bb.Min.y),
            ImVec2(mouse.x, frame_bb.Max.y),
            timeline_cursor_front, timeline_pos_cursor_w / 2.f);

        ImGui::BeginTooltipEx(ImGuiWindowFlags_None, ImGuiTooltipFlags_None);
        {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                *position = relTimelinePos;
                change = true;
                dragging = true;
            }

            videoPreview->update();
            if (SDL_GetTicks() - lastPreviewUpdate >= PreviewUpdateMs) {
                videoPreview->setPosition(relTimelinePos);
                lastPreviewUpdate = SDL_GetTicks();
            }
            const ImVec2 ImageDim = ImVec2(ImGui::GetFontSize()*7.f * (16.f / 9.f), ImGui::GetFontSize() * 7.f);
            ImGui::Image((void*)(intptr_t)videoPreview->renderTexture, ImageDim);
            float time_seconds = player->getDuration() * relTimelinePos;
            float time_delta = time_seconds - player->getCurrentPositionSecondsInterp();
            Util::FormatTime(tmp_buf[0], sizeof(tmp_buf[0]), time_seconds, false);
            Util::FormatTime(tmp_buf[1], sizeof(tmp_buf[1]), (time_delta > 0) ? time_delta : -time_delta, false);
            if (time_delta > 0)
                ImGui::Text("%s (+%s)", tmp_buf[0], tmp_buf[1]);
            else
                ImGui::Text("%s (-%s)", tmp_buf[0], tmp_buf[1]);
        }
        ImGui::EndTooltip();
    }

    if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        auto viewport = ImGui::GetWindowViewport();
        auto mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float timelineY = frame_bb.GetCenter().y;
        float timelineHeight = frame_bb.GetHeight();
        float screenY = viewport->Size.y;
        float scrubScale = 1.f - ((std::abs(timelineY - mouse.y) - timelineHeight * 1.f) / screenY);
        scrubScale = Util::Clamp(scrubScale, 0.005f, 1.f);

        if (scrubScale < 1.f) {
            scrubScale = scrubScale - 0.75f;
            scrubScale = Util::Clamp(scrubScale, 0.005f, 1.f);
            ImGui::BeginTooltipEx(ImGuiWindowFlags_None, ImGuiTooltipFlags_None);
            ImGui::Text("%s: x%0.3f", TR(SPEED), scrubScale);
            ImGui::EndTooltip();
        }

        if (std::abs(mouseDelta.x) > 0.f) {
            float startDragRelPos = (((mouse.x - mouseDelta.x) - frame_bb.Min.x) / frame_bb.GetWidth());
            float dragPosDelta = relTimelinePos - startDragRelPos;
            *position += dragPosDelta * scrubScale;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            change = true;
        }
    }
    else {
        dragging = false;
    }

    draw_list->AddLine(p1, p2, timeline_cursor_back, timeline_pos_cursor_w);
    draw_list->AddLine(p1, p2, timeline_cursor_front, timeline_pos_cursor_w / 2.f);

    constexpr float min_val = 0.f;
    constexpr float max_val = 1.f;
    if (change) { 
        *position = Util::Clamp(*position, min_val, max_val); 
    }

    return change;
}

void OFS_VideoplayerControls::DrawTimeline(bool* open, TimelineCustomDrawFunc&& customDraw) noexcept
{
    if (open != nullptr && !*open) return;
    OFS_PROFILE(__FUNCTION__);
    FUN_ASSERT(player != nullptr, "nullptr");
    ImGui::Begin(TR_ID(TimeId, Tr::TIME), open);

    {
        constexpr float speedCalcUpdateFrequency = 1.0f;
        if (!player->isPaused()) {
            if ((SDL_GetTicks() - measureStartTime) / 1000.0f >= speedCalcUpdateFrequency) {
                float duration = player->getDuration();
                float position = player->getPosition();
                float expectedStep = speedCalcUpdateFrequency / duration;
                float actualStep = std::abs(position - lastPlayerPosition);
                actualPlaybackSpeed = actualStep / expectedStep;

                lastPlayerPosition = player->getPosition();
                measureStartTime = SDL_GetTicks();
            }
        }
        else {
            lastPlayerPosition = player->getPosition();
            measureStartTime = SDL_GetTicks();
        }
    }

    ImGui::Columns(5, 0, false);
    {
        // format total duration
        // this doesn't need to be done every frame
        Util::FormatTime(tmp_buf[1], sizeof(tmp_buf[1]), player->getDuration(), true);

        double time_seconds = player->getCurrentPositionSecondsInterp();
        Util::FormatTime(tmp_buf[0], sizeof(tmp_buf[0]), time_seconds, true);
        ImGui::Text(" %s / %s (x%.03f)", tmp_buf[0], tmp_buf[1], actualPlaybackSpeed);
        ImGui::NextColumn();
    }

    auto& style = ImGui::GetStyle();
    ImGui::SetColumnWidth(0, ImGui::GetItemRectSize().x + style.ItemSpacing.x);

    if (ImGui::Button("1x", ImVec2(0, 0))) {
        player->setSpeed(1.f);
    }
    ImGui::SetColumnWidth(1, ImGui::GetItemRectSize().x + style.ItemSpacing.x);
    ImGui::NextColumn();

    if (ImGui::Button("-10%", ImVec2(0, 0))) {
        player->addSpeed(-0.10f);
    }
    ImGui::SetColumnWidth(2, ImGui::GetItemRectSize().x + style.ItemSpacing.x);
    ImGui::NextColumn();

    if (ImGui::Button("+10%", ImVec2(0, 0))) {
        player->addSpeed(0.10f);
    }
    ImGui::SetColumnWidth(3, ImGui::GetItemRectSize().x + style.ItemSpacing.x);
    ImGui::NextColumn();

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::SliderFloat("##Speed", &player->settings.playbackSpeed, 
        VideoplayerWindow::MinPlaybackSpeed, VideoplayerWindow::MaxPlaybackSpeed,
        "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        player->setSpeed(player->settings.playbackSpeed);
    }
    OFS::Tooltip(TR(SPEED));

    ImGui::Columns(1, 0, false);

    float position = player->getPosition();
    if (DrawTimelineWidget(TR_ID("TIMELINE", Tr::TIMELINE), &position, std::move(customDraw))) {
        if (!player->isPaused()) {
            hasSeeked = true;
        }
        player->setPositionPercent(position, true);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && hasSeeked) {
        player->setPaused(false);
        hasSeeked = false;
    }

    ImGui::End();
}

void OFS_VideoplayerControls::DrawControls(bool* open) noexcept
{
    if (open != nullptr && !*open) return;
    OFS_PROFILE(__FUNCTION__);
    FUN_ASSERT(player != nullptr, "nullptr");

    ImGui::Begin(TR_ID(ControlId, Tr::CONTROLS), open);

    constexpr float seekTime = 3.f;
    // Playback controls
    ImGui::Columns(5, 0, false);
    if (ImGui::Button(ICON_STEP_BACKWARD /*"<"*/, ImVec2(-1, 0))) {
        if (player->isPaused()) {
            player->previousFrame();
        }
    }
    ImGui::NextColumn();
    if (ImGui::Button(ICON_BACKWARD /*"<<"*/, ImVec2(-1, 0))) {
        player->seekRelative(-seekTime);
    }
    ImGui::NextColumn();

    if (ImGui::Button((player->isPaused()) ? ICON_PLAY : ICON_PAUSE, ImVec2(-1, 0))) {
        player->togglePlay();
    }
    ImGui::NextColumn();

    if (ImGui::Button(ICON_FORWARD /*">>"*/, ImVec2(-1, 0))) {
        player->seekRelative(seekTime);
    }
    ImGui::NextColumn();

    if (ImGui::Button(ICON_STEP_FORWARD /*">"*/, ImVec2(-1, 0))) {
        if (player->isPaused()) {
            player->nextFrame();
        }
    }
    ImGui::NextColumn();

    ImGui::Columns(2, 0, false);
    if (ImGui::Checkbox(mute ? ICON_VOLUME_OFF : ICON_VOLUME_UP, &mute)) {
        if (mute) {
            player->setVolume(0.0f);
        }
        else {
            player->setVolume(player->settings.volume);
        }
    }
    ImGui::SetColumnWidth(0, ImGui::GetItemRectSize().x + 10);
    ImGui::NextColumn();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##Volume", &player->settings.volume, 0.0f, 1.0f)) {
        player->settings.volume = Util::Clamp(player->settings.volume, 0.0f, 1.f);
        player->setVolume(player->settings.volume);
        if (player->settings.volume > 0.0f) {
            mute = false;
        }
    }
    ImGui::NextColumn();
    ImGui::End();
}
