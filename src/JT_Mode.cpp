#include "JT_Mode.h"
#include "OpenFunscripter.h"

#include "event/EventSystem.h"
#include "OFS_Videoplayer.h"
#include "OFS_ScriptTimeline.h"
#include "OFS_Util.h"

#include <opencv2/core/utils/logger.hpp>

#include "Model/Utils.h"
#include "Model/TrackingSet.h"
#include "Tracking/Trackers.h"
#include "Tracking/Runner/Tracking/TrackingRunner.h"
#include "Tracking/Calc/Calculator.h"

using namespace jt;

jt::TrackingSetPtr GetSet()
{
	auto app = OpenFunscripter::ptr;
	if (!app->LoadedProject)
		return nullptr;

	auto& bookmarks = app->LoadedProject->Settings.Bookmarks;
	float pos = app->player->getRealCurrentPositionSeconds();
	auto b = bookmarks.rbegin();
	while (b != bookmarks.rend()) {
		int bookPos = b->atS;
		if (bookPos <= pos && b->type == OFS_ScriptSettings::Bookmark::BookmarkType::START_MARKER)
		{
			if (!b->set) {
				b->set = std::make_shared<jt::TrackingSet>((time_t)(b->atS * 1000), (time_t)(b->atS * 1000));
				b->set->trackingMode = jt::TrackingMode::TM_DIAGONAL_SINGLE;
				b->set->frameSkip = 2;
			}

			return b->set;
		}
		b++;
	}

	return nullptr;
}

ImRect videoRect;
ImVec2 FloatToVideoFrame(ImVec2 input)
{
	return videoRect.Min + (videoRect.GetSize() * input);
}

ImVec2 FloatToVideoFrame(float x, float y)
{
	return FloatToVideoFrame({ x, y });
}

void MoveEndBookmark(TrackingSetPtr set)
{
	auto app = OpenFunscripter::ptr;

	OFS_ScriptSettings::Bookmark* beginMark = nullptr;
	OFS_ScriptSettings::Bookmark* endMark = nullptr;

	auto& bookmarks = app->LoadedProject->Settings.Bookmarks;
	for (auto& b : bookmarks)
	{
		if (
			!beginMark &&
			b.type == OFS_ScriptSettings::Bookmark::BookmarkType::START_MARKER &&
			b.set == set
			) {
			beginMark = &b;
			continue;
		}

		if (beginMark) {
			if (b.type != OFS_ScriptSettings::Bookmark::BookmarkType::END_MARKER)
				break;

			if (!Util::StringStartsWith(b.name, beginMark->name))
				break;

			endMark = &b;
			break;
		}
	}

	if (beginMark && endMark)
		endMark->atS = set->timeEnd / 1000;
}

void Recalc(TrackingSetPtr set, bool range = false)
{
	TrackingCalculator calculator(CalculatorInput(set->events, set->trackingMode, set->minimumMove));
	if (range)
		calculator.RecalcRange();
	else
		calculator.Recalc();
}

// RectUtil

RectUtil::RectUtil()
{
    EventSystem::ev().Subscribe(SDL_MOUSEBUTTONDOWN, EVENT_SYSTEM_BIND(this, &RectUtil::mousePressed));
	EventSystem::ev().Subscribe(SDL_MOUSEBUTTONUP, EVENT_SYSTEM_BIND(this, &RectUtil::mouseReleased));
	EventSystem::ev().Subscribe(SDL_MOUSEMOTION, EVENT_SYSTEM_BIND(this, &RectUtil::mouseDrag));
}

void RectUtil::Reset()
{
	if (state != SS_NONE)
	{
		lastRect.Min = ImVec2();
		lastRect.Max = ImVec2();
		state = SS_NONE;
	}
}

void RectUtil::mousePressed(SDL_Event& ev) noexcept
{
	TrackingSetPtr set = GetSet();

	if (!set)
		return;

	if (ev.button.button == SDL_BUTTON_RIGHT) {
		Reset();
		return;
	}

	if (ev.button.button != SDL_BUTTON_LEFT)
		return;

	ImVec2 pos = MousePosition();
	if (pos.x == 0 && pos.y == 0)
		return;
	
	if(state == SS_STARTED)
	{
		lastRect.Min = pos;
		lastRect.Max = pos;
		state = SS_P1_SELECTED;
	}
	else if(state == SS_P1_SELECTED)
	{
		float xMin = std::min(lastRect.Min.x, pos.x);
		float yMin = std::min(lastRect.Min.y, pos.y);
		float xMax = std::max(lastRect.Min.x, pos.x);
		float yMax = std::max(lastRect.Min.y, pos.y);

		lastRect.Min = ImVec2(xMin, yMin);
		lastRect.Max = ImVec2(xMax, yMax);

		state = SS_P2_SELECTED;

		TargetPtr target = std::make_shared<TrackingTarget>();
		target->initialRect.x = xMin;
		target->initialRect.y = yMin;
		target->initialRect = cv::Rect2f(xMin, yMin, xMax - xMin, yMax - yMin);
		
		target->preferredTracker = jt::TrackerJTType::CPU_RECT_KCF;

		if(!set->GetTarget(TargetType::TYPE_FEMALE))
			target->targetType = jt::TargetType::TYPE_FEMALE;
		else if (!set->GetTarget(TargetType::TYPE_MALE))
			target->targetType = jt::TargetType::TYPE_MALE;
		else
			target->targetType = jt::TargetType::TYPE_BACKGROUND;

		if (target->targetType == TargetType::TYPE_MALE)
			target->color = cv::Scalar(0.2, 0.2, 0.8, 0.2);
		else if (target->targetType == TargetType::TYPE_FEMALE)
			target->color = cv::Scalar(0.9, 0.1, 1, 0.2);
		else
			target->color = cv::Scalar(0.2, 0.2, 8, 0.3);

		set->targets.push_back(target);
		
		Reset();
	}
}

void RectUtil::mouseReleased(SDL_Event& ev) noexcept
{

}

void RectUtil::mouseDrag(SDL_Event& ev) noexcept
{
	if (state != SS_P1_SELECTED)
		return;

	ImVec2 pos = MousePosition();
	if (pos.x == 0 && pos.y == 0)
		return;
	
	lastRect.Max = pos;
}

void RectUtil::SelectRect()
{
	TrackingSetPtr set = GetSet();

	if (!set)
		return;

	lastRect.Min = ImVec2();
	lastRect.Max = ImVec2();
	state = SS_STARTED;
}

ImVec2 RectUtil::MousePosition()
{
	ImVec2 pos = ImGui::GetMousePos();
	auto app = OpenFunscripter::ptr;
	
	pos = pos -
		app->player->GetViewPort().viewportPos -
		app->player->GetViewPort().windowPos -
		app->player->GetViewPort().videoPos;

	ImVec2 vidSize = { (float)app->player->VideoWidth(), (float)app->player->VideoHeight() };
	ImVec2 vidScale = vidSize / app->player->GetViewPort().videoDrawSize;
	pos = pos * vidScale / vidSize;
	if (pos.x < 0 || pos.x > 1 || pos.y < 0 || pos.y > 1)
		return ImVec2(0, 0);

	return pos;
}

bool RectUtil::DrawButton(ImRect r, const char* id, cv::Scalar color)
{
	ImGuiID rId = ImGui::GetID(id);
	ImGui::ItemSize(r.GetSize());
	if (!ImGui::ItemAdd(r, rId))
		bool f = true;

	bool hover, pressed;
	ImGui::ButtonBehavior(r, rId, &hover, &pressed);
	if (hover || pressed)
		color[3] += 0.2;

	auto draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(
		r.Min,
		r.Max,
		ImColor((float)color[0], color[1], color[2], color[3]),
		0.f,
		ImDrawFlags_None
	);

	if (pressed)
		pressedId = rId;

	if (pressedAt.x == 0 && pressedAt.y == 0)
	{
		pressedAt = MousePosition();
		return false;
	}

	return pressed;
}

void RectUtil::DrawTargetCorners(TargetPtr target)
{
	auto& rect = target->initialRect;

	ImVec2 pos;
	ImRect c;
	std::string cid;
	
	// Corner 1
	c = ImRect(
		FloatToVideoFrame(rect.x, rect.y) - ImVec2(10, 10),
		FloatToVideoFrame(rect.x, rect.y) + ImVec2(10, 10)
	);
	cid = "rectcorner1";
	cid.append(target->GetGuid());
	if (DrawButton(c, cid.c_str(), target->color)) {
		ImVec2 pos = MousePosition();
		ImVec2 posD = pos - pressedAt;
		rect.x += posD.x;
		rect.y += posD.y;
		rect.width -= posD.x;
		rect.height -= posD.y;
		pressedAt = pos;
	}

	// Corner 2
	pos = FloatToVideoFrame(target->initialRect.x + target->initialRect.width, target->initialRect.y);
	c = ImRect(
		pos - ImVec2(10, 10),
		pos + ImVec2(10, 10)
	);
	cid = "rectcorner2";
	cid.append(target->GetGuid());
	if (DrawButton(c, cid.c_str(), target->color)) {
		ImVec2 pos = MousePosition();
		ImVec2 posD = pos - pressedAt;
		target->initialRect.width += posD.x;
		target->initialRect.y += posD.y;
		target->initialRect.height -= posD.y;
		pressedAt = pos;
	}

	// Corner 3
	pos = FloatToVideoFrame(target->initialRect.x, target->initialRect.y + target->initialRect.height);
	c = ImRect(
		pos - ImVec2(10, 10),
		pos + ImVec2(10, 10)
	);
	cid = "rectcorner3";
	cid.append(target->GetGuid());
	if (DrawButton(c, cid.c_str(), target->color)) {
		ImVec2 pos = MousePosition();
		ImVec2 posD = pos - pressedAt;
		target->initialRect.x += posD.x;
		target->initialRect.width -= posD.x;
		target->initialRect.height += posD.y;
		pressedAt = pos;
	}

	// Corner 4
	pos = FloatToVideoFrame(target->initialRect.x + target->initialRect.width, target->initialRect.y + target->initialRect.height);
	c = ImRect(
		pos - ImVec2(10, 10),
		pos + ImVec2(10, 10)
	);
	cid = "rectcorner4";
	cid.append(target->GetGuid());
	if (DrawButton(c, cid.c_str(), target->color)) {
		ImVec2 pos = MousePosition();
		ImVec2 posD = pos - pressedAt;
		target->initialRect.width += posD.x;
		target->initialRect.height += posD.y;
		pressedAt = pos;
	}
}

void RectUtil::Draw() noexcept
{
	TrackingSetPtr set = GetSet();

	if (!set)
		return;

	
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	auto draw_list = window->DrawList;

	if (state != SS_NONE)
	{
		ImVec2 mousePos = ImGui::GetMousePos();
		draw_list->AddLine(
			ImVec2(mousePos.x, videoRect.Min.y),
			ImVec2(mousePos.x, videoRect.Max.y),
			IM_COL32(0, 180, 0, 255),
			1);
		draw_list->AddLine(
			ImVec2(videoRect.Min.x, mousePos.y),
			ImVec2(videoRect.Max.x, mousePos.y),
			IM_COL32(0, 180, 0, 255),
			1);

		if (state != SS_STARTED)
		{
			ImRect r = ImRect(
				videoRect.Min + videoRect.GetSize() * lastRect.Min,
				videoRect.Min + videoRect.GetSize() * lastRect.Max
			);

			draw_list->AddRect(
				r.Min,
				r.Max,
				IM_COL32(0, 180, 0, 255),
				0.f,
				ImDrawFlags_None,
				2
			);
		}
	}
	
	pressedId = 0;
	
	for (int ri = 0; ri < set->targets.size(); ri++)
	{
		TargetPtr target = set->targets.at(ri);

		ImVec2 targetP1 = videoRect.Min + videoRect.GetSize() * ImVec2(target->initialRect.x, target->initialRect.y);
		ImVec2 targetP2 = targetP1 + ImVec2(target->initialRect.width, target->initialRect.height) * videoRect.GetSize();

		ImRect r = ImRect(targetP1, targetP2);
	
		DrawTargetCorners(target);

		if (DrawButton(r, "rect" + ri, target->color))
		{
			ImVec2 pos = MousePosition();
			ImVec2 posD = pos - pressedAt;
			target->initialRect.x += posD.x;
			target->initialRect.y += posD.y;
			pressedAt = pos;			
		}

		if (ImGui::BeginPopupContextItem())
		{
			ImGui::TextDisabled("Tracking target");

			if (ImGui::BeginCombo("Type", TargetTypeToString(target->targetType).c_str())) {
				for(int t=0; t<4; t++) {
					TargetType tt = TargetType(t);
					if (ImGui::Selectable(TargetTypeToString(tt).c_str(), target->targetType == tt)) {
						target->targetType = tt;
						if (tt == TargetType::TYPE_MALE)
							target->color = cv::Scalar(0.2, 0.2, 0.8, 0.2);
						else if (tt== TargetType::TYPE_FEMALE)
							target->color = cv::Scalar(0.9, 0.1, 1, 0.2);
						else
							target->color = cv::Scalar(0.2, 0.2, 8, 0.3);

						Recalc(set, true);
					}
				}
				ImGui::EndCombo();
			}

			TrackerJTStruct mytracker = GetTracker(target->preferredTracker);
			if (ImGui::BeginCombo("Tracker", mytracker.name.c_str())) {
				for (int t = 0; t < 12; t++) {
					TrackerJTStruct tracker = GetTracker(TrackerJTType(t));
					if (!target->SupportsTrackingType(tracker.trackingType))
						continue;
					
					if (ImGui::Selectable(tracker.name.c_str(), tracker.type == target->preferredTracker)) {
						target->preferredTracker = tracker.type;
					}
				}
				ImGui::EndCombo();
			}

			float color[4] = { (float)target->color[0], (float)target->color[1], (float)target->color[2], (float)target->color[3] };
			if (ImGui::ColorEdit4("Color", color, ImGuiColorEditFlags_NoInputs)) {
				target->color[0] = color[0];
				target->color[1] = color[1];
				target->color[2] = color[2];
				target->color[3] = color[3];
			}

			if (ImGui::MenuItem("Delete")) {
				set->targets.erase(set->targets.begin() + ri);
				ri--;
			}

			ImGui::EndPopup();
		}
	}

	if (pressedId == 0)
		pressedAt = ImVec2(0, 0);
	
}

void RectUtil::DrawRect(cv::Rect2f r, ImColor color)
{
	auto draw_list = ImGui::GetWindowDrawList();
	ImVec2 targetP1 = videoRect.Min + videoRect.GetSize() * ImVec2(r.x, r.y);
	ImVec2 targetP2 = targetP1 + ImVec2(r.width, r.height) * videoRect.GetSize();
	
	draw_list->AddRectFilled(
		targetP1,
		targetP2,
		color,
		0.f,
		ImDrawFlags_None
	);
}

// JTTrackingMode

JTTrackingMode::JTTrackingMode()
{
	auto app = OpenFunscripter::ptr;
	app->player->renderCallbacks.push_back([]() {
		auto app = OpenFunscripter::ptr;
		if (app->scripting->mode() != ScriptingModeEnum::TRACKING)
			return;

		JTTrackingMode& jt = dynamic_cast<JTTrackingMode&>(app->scripting->Impl());
		jt.DrawVideoOverlay();
	});

	EventSystem::ev().Subscribe(VideoEvents::WakeupOnMpvRenderUpdate, EVENT_SYSTEM_BIND(this, &JTTrackingMode::VideoRenderUpdate));
}

void JTTrackingMode::VideoRenderUpdate(SDL_Event& ev) noexcept
{
	auto app = OpenFunscripter::ptr;
	renderFrame = app->player->getRealCurrentPositionSeconds() - app->player->getFrameTime();
}

void JTTrackingMode::DrawVideoOverlay()
{
	auto set = GetSet();
	if (!set)
		return;

	auto app = OpenFunscripter::ptr;
	videoRect = ImRect(
		ImGui::GetItemRectMin(),
		ImGui::GetItemRectMax()
	);
	auto draw_list = ImGui::GetWindowDrawList();

	time_t pos = renderFrame * 1000;
	time_t barPos = app->player->getRealCurrentPositionSeconds() * 1000;

	if (!runner && abs(barPos - set->timeStart) < 10) {
		rectUtil.Draw();
	}
	else {
		for (auto& t : set->targets) {
			auto e = set->events->GetEvent({ pos, EventType::TET_RECT, t->GetGuid() }, true);
			ImColor color = ImColor((float)t->color[0], t->color[1], t->color[2], t->color[3]);

			if (e)
			{
				if (pos - e->time > 300)
					color = ImColor(0.9f, 0.1, 0.1, 0.5);

				rectUtil.DrawRect(e->rect, color);
			}
			else
			{
				rectUtil.DrawRect(t->initialRect , color);
			}
		}
	}

	if (showMovement)
	{
		for (auto& t : set->targets) {
			EventFilter filter(EventType::TET_POINT);
			time_t timeFrame = (app->scriptPositions.GetVisibleTime() / 2) * 1000;
			filter.timeStart = pos - timeFrame;
			filter.timeEnd = pos + timeFrame;
			filter.targetGuid = t->GetGuid();

			std::vector<cv::Point2f> cloudOld;
			std::vector<cv::Point2f> cloudNew;

			std::vector<EventPtr> events;
			set->events->GetEvents(filter, events);
			auto color = ImColor((float)t->color[0], t->color[1], t->color[2]);
			for (auto e : events)
			{
				ImColor myColor = color;

				ImVec2 center = videoRect.Min + ImVec2(
					e->point.x * videoRect.GetWidth(),
					e->point.y * videoRect.GetHeight()
				);

				if (e->time > pos)
				{
					cloudNew.push_back(e->point);
					myColor.Value.x -= 0.2;
					myColor.Value.w = (float)(e->time - pos) / timeFrame;
				}
				else
				{
					cloudOld.push_back(e->point);
					myColor.Value.x += 0.2;
					myColor.Value.w = (float)(pos - e->time) / timeFrame;
				}

				draw_list->AddCircleFilled(center, 4, myColor);
			}

			auto drawCloud = [draw_list](std::vector<cv::Point2f> cloud, ImColor color)
			{
				if (cloud.size() < 3)
					return;

				cv::Mat out;
				cv::convexHull(cloud, out);
				std::vector<cv::Point2f> vecOut(out.begin<cv::Point2f>(), out.end<cv::Point2f>());
				for (int i = 0; i < vecOut.size(); i++)
				{
					int toi = i + 1;
					if (i == vecOut.size() - 1)
						toi = 0;

					draw_list->AddLine(
						FloatToVideoFrame(vecOut.at(i).x, vecOut.at(i).y),
						FloatToVideoFrame(vecOut.at(toi).x, vecOut.at(toi).y),
						color,
						2.0f
					);
				}
			};

			ImColor colorOld = color, colorNew = color;
			colorOld.Value.x += 0.2;
			colorNew.Value.x -= 0.2;
			drawCloud(cloudOld, colorOld);
			drawCloud(cloudNew, colorNew);
		}
	}
}

void JTTrackingMode::DrawModeSettings() noexcept
{
	OFS_PROFILE(__FUNCTION__);

	auto app = OpenFunscripter::ptr;

	ImGui::Checkbox("Show tracking performance", &showPerformance);
	ImGui::Checkbox("Preview target movement", &showMovement);
	ImGui::Checkbox("Auto position simulator", &moveSimulator);

	auto pos = app->player->getRealCurrentPositionSeconds();

	if (ImGui::Button("Add tracker"))
	{
		auto& bookmarks = app->LoadedProject->Settings.Bookmarks;
		auto found = std::find_if(bookmarks.begin(), bookmarks.end(), [pos](auto& b) {
			return (int)b.atS == (int)pos;
		});

		if (found == bookmarks.end())
		{
			std::string bookmarkName = "Tracking set";
			bookmarkName.append(OFS_ScriptSettings::Bookmark::startMarker);
			OFS_ScriptSettings::Bookmark bookmark(std::move(bookmarkName), pos);
			app->LoadedProject->Settings.AddBookmark(std::move(bookmark));

			bookmarkName = "Tracking set";
			bookmarkName.append(OFS_ScriptSettings::Bookmark::endMarker);
			bookmark = OFS_ScriptSettings::Bookmark(std::move(bookmarkName), pos + app->player->getFrameTime());
			app->LoadedProject->Settings.AddBookmark(std::move(bookmark));
		}
		
		rectUtil.SelectRect();
	}

	if (runner) {
		if (ImGui::Button("Stop tracking"))
			StopTracking();
	}
	else if (ImGui::Button("Start tracking"))
		StartTracking();

	ImGui::Separator();
	auto set = GetSet();
	if (!set) {
		ImGui::TextDisabled("No set");
	}
	else {
		auto& bookmarks = app->LoadedProject->Settings.Bookmarks;
		auto bookmark = std::find_if(bookmarks.begin(), bookmarks.end(), [set](auto& b) {
			return b.set == set;
		});
		if (bookmark == bookmarks.end()) {
			ImGui::TextDisabled("No set");
		}
		else {
			ImGui::TextDisabled(bookmark->name.c_str());

			if (!runner && (pos*1000) < set->timeEnd)
			{
				if (ImGui::Button("End here"))
				{
					set->events->ClearEvents([pos](auto e) {
						return e->time < (pos * 1000);
						});
					set->timeEnd = pos * 1000;
					MoveEndBookmark(set);

					Recalc(set, true);
				}

				if (ImGui::Button("Edit range here"))
					ImGui::OpenPopup("Custom range");;

				static bool customRange = false;
				static EventPtr customRangeEvent = nullptr;

				if (ImGui::BeginPopup("Custom range")) {
					if (!customRange)
					{
						EventPtr lastRange = set->events->GetEvent(EventFilter(pos * 1000, EventType::TET_POSITION_RANGE), true);
						customRangeEvent = set->events->AddEvent(EventType::TET_POSITION_RANGE, pos * 1000);
						if (lastRange)
						{
							customRangeEvent->minDistance = lastRange->minDistance;
							customRangeEvent->maxDistance = lastRange->maxDistance;
						}

						customRange = true;
					}
					if (ImGui::SliderFloat("Min", &customRangeEvent->minDistance, 0, 1)) {
						Recalc(set);
					}

					if (ImGui::SliderFloat("Max", &customRangeEvent->maxDistance, 0, 1)) {
						Recalc(set);
					}

					if (ImGui::Button("Save")) {
						customRangeEvent = nullptr;
						customRange = false;
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}
				else if (customRange && customRangeEvent) {
					set->events->DeleteEvent(customRangeEvent);
					
					Recalc(set);
					
					customRangeEvent = nullptr;
					customRange = false;
				}
			}

			if (ImGui::Button("Copy here"))
			{
				time_t jtPos = time_t(pos * 1000.0f);

				if (set->timeEnd >= jtPos) {
					set->events->ClearEvents(EventFilter(jtPos), false);
					set->timeEnd = jtPos;
					MoveEndBookmark(set);
				}

				std::string bookmarkName = "Tracking set";
				bookmarkName.append(OFS_ScriptSettings::Bookmark::startMarker);
				OFS_ScriptSettings::Bookmark bookmark(std::move(bookmarkName), pos);

				bookmark.set = std::make_shared<jt::TrackingSet>((time_t)(jtPos), (time_t)(jtPos));
				bookmark.set->trackingMode = set->trackingMode;
				bookmark.set->minimumMove = set->minimumMove;
				bookmark.set->frameSkip = set->frameSkip;

				for (auto& t : set->targets) {
					auto e = set->events->GetEvent({ jtPos, EventType::TET_RECT, t->GetGuid() }, true);
					if (!e)
						continue;

					TargetPtr target = std::make_shared<TrackingTarget>();
					target->color = t->color;
					target->initialRect = e->rect;
					target->preferredTracker = t->preferredTracker;
					target->targetType = t->targetType;
					target->range = t->range;
					bookmark.set->targets.push_back(target);
				}

				app->LoadedProject->Settings.AddBookmark(std::move(bookmark));

				bookmarkName = "Tracking set";
				bookmarkName.append(OFS_ScriptSettings::Bookmark::endMarker);
				OFS_ScriptSettings::Bookmark endBookmark = OFS_ScriptSettings::Bookmark(std::move(bookmarkName), pos + app->player->getFrameTime());
				app->LoadedProject->Settings.AddBookmark(std::move(endBookmark));

				return;
			}
			
			if (ImGui::SliderFloat("Minimum movement", &set->minimumMove, 0, 1)) {
				Recalc(set);
			}

			if (ImGui::BeginCombo("Frame skip", std::to_string(set->frameSkip).c_str())) {
				for (int t = 0; t < 4; t++) {
					if (ImGui::Selectable(std::to_string(t).c_str(), t == set->frameSkip)) {
						set->frameSkip = t;
					}
				}
				ImGui::EndCombo();
			}

			if (ImGui::BeginCombo("Tracking mode", TrackingModeToString(set->trackingMode).c_str())) {
				for (int t = 0; t < 7; t++) {
					TrackingMode tt = TrackingMode(t);
					if (ImGui::Selectable(TrackingModeToString(tt).c_str(), set->trackingMode == tt)) {
						set->trackingMode = tt;

						Recalc(set, true);
					}
				}
				ImGui::EndCombo();
			}
		}
	}

	DrawPerformance();
}

void JTTrackingMode::DrawPerformance()
{
	if (!showPerformance)
		return;

	if (!ImGui::Begin("Tracking performance", &showPerformance, ImGuiWindowFlags_None))
		return;

	if (!runner) {
		ImGui::Text("Status: Idle");
		ImGui::End();
		return;
	}
	else if (!runner->GetState().lastJob) {
		ImGui::Text("Status: Starting");
		ImGui::End();
		return;
	}
	else {
		ImGui::Text("Status: Tracking");
	}

	auto bb = ImGui::GetWindowSize();

	float data[200] = { 0 };
	for (int i = 0; i < IM_ARRAYSIZE(data) && i < jobLog.size(); i++)
		data[i] = jobLog.at(i);

	ImVec2 plotSize(bb.x - 150, 50);

	char overlay[32];
	sprintf(overlay, "%0.0f ms", data[0]);
	ImGui::PlotLines("Job", data, IM_ARRAYSIZE(data), 0, overlay, FLT_MAX, FLT_MAX, plotSize);

	auto work = runner->GetState().lastJob->GetWork<Work>();
	for (int w = 0; w < work.size(); w++)
	{
		if (threadLog.size() <= w)
			break;

		memset(data, 0, sizeof(float) * IM_ARRAYSIZE(data));
		for (int i = 0; i < IM_ARRAYSIZE(data) && i < jobLog.size(); i++)
			data[i] = threadLog.at(w).at(i);
		
		sprintf(overlay, "%0.0f ms", data[0]);
		ImGui::PlotLines(work.at(w)->ThreadName().c_str(), data, IM_ARRAYSIZE(data), 0, overlay, FLT_MAX, FLT_MAX, plotSize);
	}

	ImGui::End();
	return;
}

void JTTrackingMode::MoveSimulator()
{
	auto app = OpenFunscripter::ptr;
	auto set = GetSet();
	if (!set)
		return;

	time_t pos = renderFrame * 1000;

	EventPtr maleEvent, femaleEvent;
	ImVec2 malePos, femalePos;

	maleEvent = set->events->GetEvent(EventFilter(pos, EventType::TET_POINT, TargetType::TYPE_MALE), true);
	if (maleEvent)
	{
		malePos = videoRect.Min + ImVec2(
			maleEvent->point.x * videoRect.GetWidth(),
			maleEvent->point.y * videoRect.GetHeight()
		);
	}

	femaleEvent = set->events->GetEvent(EventFilter(pos, EventType::TET_POINT, TargetType::TYPE_FEMALE), true);
	if (femaleEvent)
	{
		femalePos = videoRect.Min + ImVec2(
			femaleEvent->point.x * videoRect.GetWidth(),
			femaleEvent->point.y * videoRect.GetHeight()
		);
	}

	float centerY = (femalePos.y + malePos.y) / 2;
	float angle;

	switch (set->trackingMode) {
	case TrackingMode::TM_HORIZONTAL:
		if (!maleEvent || !femaleEvent)
			break;

		//angle = atan2f(femalePos.y - malePos.y, femalePos.x - malePos.x);
		app->simulator.simulator.P2.x = malePos.x;
		//app->simulator.simulator.P2.x = femalePos.x;

		app->simulator.simulator.P1.y = centerY;
		app->simulator.simulator.P2.y = centerY;
		break;
	}
}

void JTTrackingMode::StopTracking()
{
	if (runner)
		delete runner.release();

	rectUtil.Reset();
	WritePositions();
}

void JTTrackingMode::StartTracking()
{
	auto app = OpenFunscripter::ptr;

	jt::TrackingSetPtr set = GetSet();
	if (!set)
		return;

	if (runner)
		delete runner.release();

	threadLog.clear();
	jobLog.clear();
	//cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_VERBOSE);

	if (!reader)
		reader = jt::VideoReader::create(app->player->getVideoPath());

	this->set = set;
	runner = std::make_unique<jt::TrackingRunner>(set, nullptr, reader, true, false);
	if (!runner->Setup())
	{
		bool f = true;
		return;
	}

	runner->SetRunning(true);
	app->player->setPaused(true);
}

void JTTrackingMode::addEditAction(FunscriptAction action) noexcept
{

}

void JTTrackingMode::undo() noexcept
{

}

void JTTrackingMode::redo() noexcept
{

}

void JTTrackingMode::update() noexcept
{
	auto app = OpenFunscripter::ptr;

	if(moveSimulator)
		MoveSimulator();

	if (!runner)
		return;

	if (app->scripting->mode() != ScriptingModeEnum::TRACKING)
		StopTracking();

	runner->Update();

	auto state = runner->GetState();
	if (state.end)
	{
		delete runner.release();
		app->player->relativeFrameSeek(set->frameSkip + 1);
		return;
	}

	if (!app->player->isPaused())
	{
		app->player->setPaused(true);
		StopTracking();
		return;
	}

	if (state.framesRdy > 0)
	{
		app->player->setPositionExact((float)state.lastJob->time / 1000);

		std::vector<ImRect> rects;

		auto jobs = state.lastJob->GetWork<jt::TrackingWork>();
		
		for (auto j : jobs)
		{
			ImRect newRect;
			newRect.Min = {
				(float)j->state.rect.x / app->player->VideoWidth(),
				(float)j->state.rect.y / app->player->VideoHeight()
			};
			newRect.Max = {
				(float)(j->state.rect.x + j->state.rect.width) / app->player->VideoWidth(),
				(float)(j->state.rect.y + j->state.rect.height) / app->player->VideoHeight()
			};

			rects.push_back(newRect);
		}

		auto work = state.lastJob->GetWork<Work>();
		for (int w = 0; w < work.size(); w++)
		{
			if (threadLog.size() <= w)
				threadLog.emplace_back();

			threadLog.at(w).push_front(work.at(w)->GetDurationMs());
			while (threadLog.at(w).size() > 200)
				threadLog.at(w).pop_back();
		}

		jobLog.push_front(state.lastJob->GetDurationMs());
		while (jobLog.size() > 200)
			jobLog.pop_back();

		runner->GetState(true);
	}

	MoveEndBookmark(set);

	WritePositions();
}

void JTTrackingMode::WritePositions()
{
	auto set = GetSet();
	if (!set)
		return;

	auto app = OpenFunscripter::ptr;
	std::lock_guard<std::mutex> lock(set->events->mtx);
	auto& script = app->script();

	script.RemoveActions(script.GetSelection((float)set->timeStart / 1000, (float)set->timeEnd / 1000));
	std::vector<std::shared_ptr<jt::TrackingEvent>> events;

	jt::EventFilter filter(jt::EventType::TET_POSITION);

	set->events->GetEvents(filter, events);
	for (auto e : events)
	{
		FunscriptAction a((float)e->time / 1000, e->position * 100);
		script.AddAction(a);
	}
}


// ScriptTrackingOverlay

ScriptTrackingOverlay::ScriptTrackingOverlay(ScriptTimeline* timeline)
	: FrameOverlay(timeline) 
{
	EventSystem::ev().Subscribe(ScriptTimelineEvents::FunscriptSelectTime, EVENT_SYSTEM_BIND(this, &ScriptTrackingOverlay::OnSelection));
}

Funscript tmpScript;

void ScriptTrackingOverlay::DrawSettings() noexcept
{
	ImGui::Checkbox("Draw all tracking modes", &showAllModes);
}

void ScriptTrackingOverlay::DrawScriptPositionContent(const OverlayDrawingCtx& ctx) noexcept
{
	auto app = OpenFunscripter::ptr;
	auto frameTime = app->player->getFrameTime();

	float visibleFrames = ctx.visibleTime / frameTime;
	constexpr float maxVisibleFrames = 400.f;

	if (visibleFrames <= (maxVisibleFrames * 0.75f)) {
		//render frame dividers
		float offset = -std::fmod(ctx.offsetTime, frameTime);
		const int lineCount = visibleFrames + 2;
		int alpha = 255 * (1.f - (visibleFrames / maxVisibleFrames));
		for (int i = 0; i < lineCount; i++) {
			ctx.draw_list->AddLine(
				ctx.canvas_pos + ImVec2(((offset + (i * frameTime)) / ctx.visibleTime) * ctx.canvas_size.x, 0.f),
				ctx.canvas_pos + ImVec2(((offset + (i * frameTime)) / ctx.visibleTime) * ctx.canvas_size.x, ctx.canvas_size.y),
				IM_COL32(80, 80, 80, alpha),
				1.f
			);
		}
	}

	// time dividers
	constexpr float maxVisibleTimeDividers = 150.f;
	const float timeIntervalMs = std::round(app->player->getFps() * 0.1f) * frameTime;
	const float visibleTimeIntervals = ctx.visibleTime / timeIntervalMs;
	if (visibleTimeIntervals <= (maxVisibleTimeDividers * 0.8f)) {
		float offset = -std::fmod(ctx.offsetTime, timeIntervalMs);
		const int lineCount = visibleTimeIntervals + 2;
		int alpha = 255 * (1.f - (visibleTimeIntervals / maxVisibleTimeDividers));
		for (int i = 0; i < lineCount; i++) {
			ctx.draw_list->AddLine(
				ctx.canvas_pos + ImVec2(((offset + (i * timeIntervalMs)) / ctx.visibleTime) * ctx.canvas_size.x, 0.f),
				ctx.canvas_pos + ImVec2(((offset + (i * timeIntervalMs)) / ctx.visibleTime) * ctx.canvas_size.x, ctx.canvas_size.y),
				IM_COL32(80, 80, 80, alpha),
				3.f
			);
		}
	}

	BaseOverlay::DrawHeightLines(ctx);
	timeline->DrawAudioWaveform(ctx);

	auto set = GetSet();
	if (set)
	{
		if (!showAllModes)
			DrawActionLines(ctx, *set->events);
		else
		{
			for (int t = 0; t < 7; t++) {
				TrackingMode tt = TrackingMode(t);
				EventList eventList;
				std::vector<EventPtr> events;
				set->events->GetEvents(EventFilter(EventType::TET_UNKNOWN), events);
				for (auto e : events)
					eventList.AddEvent(e);

				TrackingCalculator calculator(CalculatorInput(eventList, tt));
				calculator.RecalcRange();

				DrawActionLines(ctx, eventList);
			}
		}
	}
	
	BaseOverlay::DrawSecondsLabel(ctx);
	BaseOverlay::DrawScriptLabel(ctx);
	DrawBookmarks(ctx);
	DrawEvents(ctx);
}

bool ScriptTrackingOverlay::HandleDelete() noexcept
{
	if (selectFrom && selectTo)
	{
		auto set = GetSet();
		if (set)
		{
			EventFilter filter;
			filter.eventTypes = (1 << EventType::TET_BADFRAME) | (1 << EventType::TET_POSITION_RANGE);
			filter.timeStart = selectFrom;
			filter.timeEnd = selectTo;

			set->events->ClearEvents(filter, false);

			selectFrom = 0;
			selectTo = 0;

			Recalc(set);
		}
	}


	return true;
}

void ScriptTrackingOverlay::OnSelection(SDL_Event& ev) noexcept
{
	ScriptTimelineEvents::SelectTime& params = *(ScriptTimelineEvents::SelectTime*)ev.user.data1;
	if (params.mode != ScriptTimelineEvents::Mode::All)
		return;

	auto app = OpenFunscripter::ptr;
	app->scriptPositions.absSel1 = 0;
	app->scriptPositions.relSel2 = 0;
	app->script().ClearSelection();

	selectFrom = params.startTime * 1000;
	selectTo = params.endTime * 1000;
}

void ScriptTrackingOverlay::DrawEvents(const OverlayDrawingCtx& ctx) noexcept
{
	auto app = OpenFunscripter::ptr;

	float visableHalf = ctx.visibleTime / 2;
	EventFilter filter;
	filter.eventTypes = (1 << EventType::TET_BADFRAME) | (1 << EventType::TET_POSITION_RANGE);
	filter.timeStart = (app->player->getRealCurrentPositionSeconds() - visableHalf) * 1000;
	filter.timeEnd = (app->player->getRealCurrentPositionSeconds() + visableHalf) * 1000;

	auto set = GetSet();
	if (!set)
		return;

	std::vector<EventPtr> events;
	set->events->GetEvents(filter, events);

	ImRect lastItemSize(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	ImGuiID lastItemId = ImGui::GetItemID();
	
	for (EventPtr e : events)
	{
		float relative_x = (float)(((float)e->time / 1000) - ctx.offsetTime) / ctx.visibleTime;
		float x = (ctx.canvas_size.x) * relative_x;

		char idStr[100];
		snprintf(idStr, sizeof(idStr), "event%llu", (intptr_t)e.get());
		ImGuiID id = ImGui::GetID(idStr);
		ImRect rect = ImRect(
			ctx.canvas_pos + ImVec2(x - 2, 0),
			ctx.canvas_pos + ImVec2(x + 2, ctx.canvas_size.y)
		);

		ImGui::PushID(id);
		ImGui::ItemAdd(rect, id);
		ImGui::ItemSize(rect);

		if (ImGui::BeginPopupContextItem(idStr))
		{
			if (e->type == EventType::TET_POSITION_RANGE)
			{
				if (ImGui::SliderFloat("Min", &e->minDistance, 0, 1)) {
					Recalc(set);
				}

				if (ImGui::SliderFloat("Max", &e->maxDistance, 0, 1)) {
					Recalc(set);
				}
			}

			if (ImGui::MenuItem("Delete")) {
				set->events->DeleteEvent(e);
				Recalc(set);
			}
			ImGui::EndPopup();
		}

		if (e->type == EventType::TET_BADFRAME)
		{
			auto color = ImColor(1.0f, 0.0f, 0.0f);
			if (e->time >= selectFrom && e->time <= selectTo)
				color = ImColor(1.0f, 0.7f, 0.7f);

			ctx.draw_list->AddLine(
				ctx.canvas_pos + ImVec2(x, 0.f),
				ctx.canvas_pos + ImVec2(x, ctx.canvas_size.y),
				color,
				4.f
			);
		}

		if (e->type == EventType::TET_POSITION_RANGE)
		{
			auto color = ImColor(0.0f, 0.0f, 1.0f);
			if (e->time >= selectFrom && e->time <= selectTo)
				color = ImColor(0.5f, 0.5f, 0.1f);

			ctx.draw_list->AddLine(
				ctx.canvas_pos + ImVec2(x, 0.f),
				ctx.canvas_pos + ImVec2(x, ctx.canvas_size.y),
				color,
				4.f
			);
		}

		ImGui::PopID();

		
	}

	// Restore timeline size
	ImGui::ItemAdd(lastItemSize, lastItemId);
	ImGui::ItemSize(lastItemSize);
}

void ScriptTrackingOverlay::DrawActionLines(const OverlayDrawingCtx& ctx, EventList& eventList) noexcept
{
	auto app = OpenFunscripter::ptr;
	
	ColoredLines.clear();

	float visableHalf = ctx.visibleTime / 2;
	EventFilter filter(EventType::TET_POSITION);
	filter.timeStart = (app->player->getRealCurrentPositionSeconds() - visableHalf) * 1000;
	filter.timeEnd = (app->player->getRealCurrentPositionSeconds() + visableHalf) * 1000;

	std::vector<EventPtr> events;
	eventList.GetEvents(filter, events);

	auto getPointForAction = [](const OverlayDrawingCtx& ctx, EventPtr e) {
		float atS = (float)e->time / 1000;
		float relative_x = (float)(atS - ctx.offsetTime) / ctx.visibleTime;
		float x = (ctx.canvas_size.x) * relative_x;
		float y = (ctx.canvas_size.y) * (1 - e->position);
		x += ctx.canvas_pos.x;
		y += ctx.canvas_pos.y;
		return ImVec2(x, y);
	};

	EventPtr lastEvent;
	for (EventPtr e : events)
	{
		auto p1 = getPointForAction(ctx, e);

		if (lastEvent != nullptr) {
			// draw line
			auto p2 = getPointForAction(ctx, lastEvent);
			// calculate speed relative to maximum speed
			float rel_speed = Util::Clamp<float>(((std::abs(e->position - lastEvent->position)*100) / (((float)e->time - (float)lastEvent->time)/1000)) / HeatmapGradient::MaxSpeedPerSecond, 0.f, 1.f);
			ImColor speed_color;
			speedGradient.getColorAt(rel_speed, &speed_color.Value.x);
			speed_color.Value.w = 1.f;

			ctx.draw_list->AddLine(p1, p2, IM_COL32(0, 0, 0, 255), 7.0f); // border
			ColoredLines.emplace_back(std::move(BaseOverlay::ColoredLine{ p1, p2, ImGui::ColorConvertFloat4ToU32(speed_color) }));
		}

		lastEvent = e;
	}

	// this is so that the black background line gets rendered first
	for (auto&& line : ColoredLines) {
		ctx.draw_list->AddLine(line.p1, line.p2, line.color, 3.f);
	}
}

int rotation_start_index;
void ImRotateStart()
{
	rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

ImVec2 ImRotationCenter()
{
	ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX); // bounds

	const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
	for (int i = rotation_start_index; i < buf.Size; i++)
		l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);

	return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2); // or use _ClipRectStack?
}

void ImRotateEnd(float rad, ImVec2 center = ImRotationCenter())
{
	float s = sin(rad), c = cos(rad);
	center = ImRotate(center, s, c) - center;

	auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
	for (int i = rotation_start_index; i < buf.Size; i++)
		buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

void ScriptTrackingOverlay::DrawBookmarks(const OverlayDrawingCtx& ctx) noexcept
{
	auto app = OpenFunscripter::ptr;
	auto& bookmarks = app->LoadedProject->Settings.Bookmarks;
	float timeStart = app->player->getCurrentPositionSecondsInterp() - app->scriptPositions.GetVisibleTime() / 2;
	float timeEnd = app->player->getCurrentPositionSecondsInterp() + app->scriptPositions.GetVisibleTime() / 2;

	for (int i = 0; i < bookmarks.size(); i++)
	{
		auto& b = bookmarks.at(i);

		if (b.atS < timeStart || b.atS > timeEnd)
			continue;

		float relative_x = (float)(b.atS - ctx.offsetTime) / ctx.visibleTime;
		float x = (ctx.canvas_size.x) * relative_x;
		auto color = IM_COL32(255, 255, 255, 153);
		
		bool hover = false;
		/*
		app->BookmarkPopup(
			ctx.canvas_pos + ImVec2(x, 0.f) - ImVec2(2.5, 2.5),
			ctx.canvas_pos + ImVec2(x, ctx.canvas_size.y) + ImVec2(2.5, 2.5),
			hover,
			ImGui::GetID("bmtimeline"+i),
			&b,
			nullptr
		);
		*/

		if (hover)
			color = IM_COL32(255, 255, 255, 80);

		ctx.draw_list->AddLine(
			ctx.canvas_pos + ImVec2(x, 0.f),
			ctx.canvas_pos + ImVec2(x, ctx.canvas_size.y),
			color,
			5.f
		);
		
		auto& style = ImGui::GetStyle();

		//ImRotateStart();
		ctx.draw_list->AddText(OFS_DynFontAtlas::DefaultFont2, app->settings->data().default_font_size,
			ctx.canvas_pos + ImVec2(x, 0.f),
			ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]),
			b.name.c_str()
		);
		//ImRotateEnd(1.047);
	}
}
