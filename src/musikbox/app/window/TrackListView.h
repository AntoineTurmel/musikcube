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

#pragma once

#include <cursespp/curses_config.h>
#include <cursespp/ScrollAdapterBase.h>
#include <cursespp/IKeyHandler.h>
#include <cursespp/ListWindow.h>

#include <core/library/query/local/TrackListQueryBase.h>
#include <core/audio/PlaybackService.h>

#include <core/runtime/IMessage.h>
#include <core/library/ILibrary.h>

namespace musik {
    namespace box {
        class TrackListView :
            public cursespp::ListWindow,
#if (__clang_major__ == 7 && __clang_minor__ == 3)
            public std::enable_shared_from_this<TrackListView>,
#endif
            public sigslot::has_slots<>
        {
            public:
                sigslot::signal1<musik::core::db::local::TrackListQueryBase*> Requeried;

                typedef std::function<std::string(
                    musik::core::TrackPtr, size_t)> RowFormatter;

                typedef std::function<int64(
                    musik::core::TrackPtr, size_t)> RowDecorator;

                typedef std::shared_ptr<std::set<size_t> > Headers;

                typedef musik::core::db::local::TrackListQueryBase TrackListQueryBase;

                TrackListView(
                    musik::core::audio::PlaybackService& playback,
                    musik::core::ILibraryPtr library,
                    RowFormatter formatter = RowFormatter(),
                    RowDecorator decorator = RowDecorator());

                virtual ~TrackListView();

                virtual void ProcessMessage(musik::core::runtime::IMessage &message);
                virtual bool KeyPress(const std::string& key);

                std::shared_ptr<const musik::core::TrackList> GetTrackList();
                void SetTrackList(std::shared_ptr<const musik::core::TrackList> trackList);

                void Clear();
                musik::core::TrackPtr Get(size_t index);
                size_t Count();

                void Requery(std::shared_ptr<TrackListQueryBase> query);

            protected:
                virtual cursespp::IScrollAdapter& GetScrollAdapter();
                void OnQueryCompleted(musik::core::db::IQuery* query);

                class Adapter : public cursespp::ScrollAdapterBase {
                    public:
                        Adapter(TrackListView &parent);
                        virtual ~Adapter() { }

                        virtual size_t GetEntryCount();
                        virtual EntryPtr GetEntry(cursespp::ScrollableWindow* window, size_t index);

                    private:
                        TrackListView &parent;
                        IScrollAdapter::ScrollPosition spos;
                };

            private:
                void OnTrackChanged(size_t index, musik::core::TrackPtr track);
                void ScrollToPlaying();

                std::shared_ptr<TrackListQueryBase> query;
                std::shared_ptr<const musik::core::TrackList> metadata;
                Headers headers;
                Adapter* adapter;
                musik::core::audio::PlaybackService& playback;
                musik::core::TrackPtr playing;
                musik::core::ILibraryPtr library;
                size_t lastQueryHash;
                RowFormatter formatter;
                RowDecorator decorator;
                std::chrono::milliseconds lastChanged;
        };
    }
}
