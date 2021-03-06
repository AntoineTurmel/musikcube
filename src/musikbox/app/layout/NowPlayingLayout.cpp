//////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2007-2016 musikcube team
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the author nor the names of other contributors may
//      be used to endorse or promote products derived from this software
//      without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include <cursespp/Colors.h>
#include <cursespp/Screen.h>
#include <cursespp/Text.h>
#include <core/library/LocalLibraryConstants.h>
#include <app/util/Hotkeys.h>
#include <app/overlay/PlayQueueOverlays.h>
#include <core/library/query/local/NowPlayingTrackListQuery.h>
#include <core/library/query/local/GetPlaylistQuery.h>
#include <glue/util/Duration.h>
#include "NowPlayingLayout.h"

#include <set>
#include <boost/format.hpp>

using namespace musik::core::library::constants;

using namespace musik::core;
using namespace musik::core::audio;
using namespace musik::core::library;
using namespace musik::box;
using namespace musik::core::db::local;
using namespace musik::glue;
using namespace cursespp;

static std::set<std::string> EDIT_KEYS;

static std::string formatWithAlbum(TrackPtr track, size_t width);

NowPlayingLayout::NowPlayingLayout(
    musik::core::audio::PlaybackService& playback,
    musik::core::ILibraryPtr library)
: LayoutBase()
, playback(playback)
, library(library)
, lastPlaylistQueryId(-1) {
    this->InitializeWindows();

    this->playback.QueueEdited.connect(this, &NowPlayingLayout::RequeryTrackList);

    EDIT_KEYS = {
        Hotkeys::Get(Hotkeys::PlayQueueMoveUp),
        Hotkeys::Get(Hotkeys::PlayQueueMoveDown),
        Hotkeys::Get(Hotkeys::PlayQueueDelete)
    };
}

NowPlayingLayout::~NowPlayingLayout() {

}

int64 NowPlayingLayout::RowDecorator(musik::core::TrackPtr track, size_t index) {
    bool selected = index == trackListView->GetSelectedIndex();

    int64 attrs = selected
        ? COLOR_PAIR(CURSESPP_HIGHLIGHTED_LIST_ITEM)
        : CURSESPP_DEFAULT_COLOR;

    size_t playingIndex = playback.GetIndex();

    if (index == playingIndex) {
        TrackPtr playing = playback.GetPlaying();

        if (playing &&
            playing->GetId() == track->GetId() &&
            playing->LibraryId() == track->LibraryId())
        {
            if (selected) {
                attrs = COLOR_PAIR(CURSESPP_HIGHLIGHTED_SELECTED_LIST_ITEM);
            }
            else {
                attrs = COLOR_PAIR(CURSESPP_SELECTED_LIST_ITEM);
            }
        }
    }

    return attrs;
}

void NowPlayingLayout::OnLayout() {
    this->trackListView->MoveAndResize(
        0,
        0,
        this->GetWidth(),
        this->GetHeight());

    this->trackListView->SetFocusOrder(1);
}

void NowPlayingLayout::InitializeWindows() {
    this->trackListView.reset(new TrackListView(
        this->playback,
        this->library,
        std::bind(formatWithAlbum, std::placeholders::_1, std::placeholders::_2),
        std::bind(&NowPlayingLayout::RowDecorator, this, std::placeholders::_1, std::placeholders::_2)));

    this->trackListView->Requeried.connect(this, &NowPlayingLayout::OnTrackListRequeried);
    this->AddWindow(this->trackListView);
}

void NowPlayingLayout::OnVisibilityChanged(bool visible) {
    LayoutBase::OnVisibilityChanged(visible);

    if (visible) {
        this->RequeryTrackList();
    }
    else {
        this->trackListView->Clear();
    }
}

void NowPlayingLayout::OnTrackListRequeried(musik::core::db::local::TrackListQueryBase* query) {
    /* in most cases we pull the TrackList directly from the PlaybackService.
    however, some user operations cause the TrackList to be loaded from
    the database, e.g. loading regular playlists. in these cases, copy
    the tracks TO the PlaybackService, then refresh! */
    if (query && query->GetId() == this->lastPlaylistQueryId) {
        this->playback.CopyFrom(*query->GetResult());
        this->lastPlaylistQueryId = -1;
    }

    if (playback.Count()) {
        /* regular logic (i.e, no edit operation) */
        if (this->reselectIndex == -1) {
            size_t index = playback.GetIndex();

            if (index == ListWindow::NO_SELECTION) { /* not playing? */
                this->trackListView->SetSelectedIndex(0);
                this->trackListView->ScrollTo(0);
            }
            else { /* playing... */
                this->trackListView->SetSelectedIndex(index);
                this->trackListView->ScrollTo(index == 0 ? index : index - 1);
            }
        }
        /* user just finished an edit. this is a bit of a hack; we're notified
        of the edit completion asynchronously, so before we complete the edit
        we stash the index we need to re-select. */
        else {
            /* ensure the correct index is selected, and that it's properly
            scrolled into view */
            this->reselectIndex = std::min((int) this->trackListView->Count() - 1, this->reselectIndex);
            this->trackListView->SetSelectedIndex((size_t)this->reselectIndex);
            auto pos = this->trackListView->GetScrollPosition();
            int first = (int)pos.firstVisibleEntryIndex;
            int last = (int)first + pos.visibleEntryCount;
            int index = (int)this->reselectIndex;
            if (index < first || index >= last) {
                this->trackListView->ScrollTo((size_t)this->reselectIndex);
            }
            this->reselectIndex = -1;
        }

        /* if after a bunch of monkeying around there's still nothing
        selected, but we have contents, let's just select the first item */
        auto sel = this->trackListView->GetSelectedIndex();
        if (sel == ListWindow::NO_SELECTION || sel >= this->trackListView->Count()) {
            this->trackListView->SetSelectedIndex(0);
            this->trackListView->ScrollTo(0);
        }
    }
    else {
        this->trackListView->ScrollTo(0);
    }
}

void NowPlayingLayout::RequeryTrackList() {
    this->trackListView->SetTrackList(this->playback.GetTrackList());
    this->OnTrackListRequeried(nullptr);
}

void NowPlayingLayout::OnPlaylistSelected(DBID playlistId) {
    auto query = std::shared_ptr<GetPlaylistQuery>(
        new GetPlaylistQuery(library, playlistId));

    this->lastPlaylistQueryId = query->GetId();
    this->trackListView->Requery(query);
}

bool NowPlayingLayout::KeyPress(const std::string& key) {
    if (key == "KEY_ENTER") {
        size_t index = this->trackListView->GetSelectedIndex();
        if (index != ListWindow::NO_SELECTION) {
            this->playback.Play(index);
            return true;
        }
    }
    else if (Hotkeys::Is(Hotkeys::PlayQueuePlaylistLoad, key)) {
        PlayQueueOverlays::ShowLoadPlaylistOverlay(
            this->playback,
            this->library,
            std::bind(&NowPlayingLayout::OnPlaylistSelected, this, std::placeholders::_1));
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::PlayQueuePlaylistSave, key)) {
        PlayQueueOverlays::ShowSavePlaylistOverlay(this->playback, this->library);
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::PlayQueuePlaylistRename, key)) {
        PlayQueueOverlays::ShowRenamePlaylistOverlay(this->library);
        return true;
    }
    else if (Hotkeys::Is(Hotkeys::PlayQueuePlaylistDelete, key)) {
        PlayQueueOverlays::ShowDeletePlaylistOverlay(this->library);
        return true;
    }
    else if (ProcessEditOperation(key)) {
        return true;
    }

    return LayoutBase::KeyPress(key);
}

bool NowPlayingLayout::ProcessEditOperation(const std::string& key) {
    if (EDIT_KEYS.find(key) != EDIT_KEYS.end()) {
        if (!playback.IsShuffled()) {
            size_t selected = this->trackListView->GetSelectedIndex();
            this->reselectIndex = -1;

            {
                PlaybackService::Editor editor = this->playback.Edit();
                if (Hotkeys::Is(Hotkeys::PlayQueueMoveUp, key)) {
                    if (selected > 0) {
                        size_t to = selected - 1;
                        editor.Move(selected, to);
                        reselectIndex = (int)to;
                    }
                }
                else if (Hotkeys::Is(Hotkeys::PlayQueueMoveDown, key)) {
                    if (selected < this->playback.Count() - 1) {
                        size_t to = selected + 1;
                        editor.Move(selected, to);
                        reselectIndex = (int)to;
                    }
                }
                else if (Hotkeys::Is(Hotkeys::PlayQueueDelete, key)) {
                    editor.Delete(selected);
                    reselectIndex = (int)selected;
                }
            }

            return true;
        }
    }

    return false;
}

#define TRACK_COL_WIDTH 3
#define ARTIST_COL_WIDTH 14
#define ALBUM_COL_WIDTH 14
#define DURATION_COL_WIDTH 5 /* 00:00 */

static std::string formatWithAlbum(TrackPtr track, size_t width) {
    std::string trackNum = text::Align(
        track->GetValue(constants::Track::TRACK_NUM),
        text::AlignRight,
        TRACK_COL_WIDTH);

    std::string duration = text::Align(
        duration::Duration(track->GetValue(constants::Track::DURATION)),
        text::AlignRight,
        DURATION_COL_WIDTH);

    std::string album = text::Align(
        track->GetValue(constants::Track::ALBUM),
        text::AlignLeft,
        ALBUM_COL_WIDTH);

    std::string artist = text::Align(
        track->GetValue(constants::Track::ARTIST),
        text::AlignLeft,
        ARTIST_COL_WIDTH);

    int titleWidth =
        width -
        TRACK_COL_WIDTH -
        DURATION_COL_WIDTH -
        ALBUM_COL_WIDTH -
        ARTIST_COL_WIDTH -
        (4 * 3); /* 3 = spacing */

    titleWidth = std::max(0, titleWidth);

    std::string title = text::Align(
        track->GetValue(constants::Track::TITLE),
        text::AlignLeft,
        (int) titleWidth);

    return boost::str(
        boost::format("%s   %s   %s   %s   %s")
        % trackNum % title % duration % album % artist);
}