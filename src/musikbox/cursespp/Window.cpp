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

#include <stdafx.h>
#include "Window.h"
#include "IWindowGroup.h"
#include "IInput.h"
#include "Colors.h"
#include "Screen.h"

#include <core/runtime/Message.h>
#include <core/runtime/MessageQueue.h>

using namespace cursespp;
using namespace musik::core::runtime;

static int NEXT_ID = 0;

static bool drawPending = false;
static bool freeze = false;
static Window* top = nullptr;

static MessageQueue messageQueue;

#define ENABLE_BOUNDS_CHECK 1

static inline void DrawCursor(IInput* input) {
    if (input) {
        Window* inputWindow = dynamic_cast<Window*>(input);
        if (inputWindow) {
            WINDOW* content = inputWindow->GetContent();
            curs_set(1);
            wtimeout(content, IDLE_TIMEOUT_MS);
            wmove(content, 0, input->Position());
        }
    }
    else {
        curs_set(0);
    }
}

bool Window::WriteToScreen(IInput* input) {
    if (drawPending && !freeze) {
        drawPending = false;
        update_panels();
        doupdate();
        DrawCursor(input);
        return true;
    }

    return false;
}

void Window::InvalidateScreen() {
    wclear(stdscr);
    drawPending = true;
}

void Window::Freeze() {
    if (!freeze) {
        freeze = true;
        Window::InvalidateScreen();
    }
}

void Window::Unfreeze() {
    if (freeze) {
        freeze = false;
        Window::InvalidateScreen();
    }
}

IMessageQueue& Window::MessageQueue() {
    return messageQueue;
}

Window::Window(IWindow *parent) {
    this->frame = this->content = 0;
    this->framePanel = this->contentPanel = 0;
    this->parent = parent;
    this->height = 0;
    this->width = 0;
    this->x = 0;
    this->y = 0;
    this->lastAbsoluteX = 0;
    this->lastAbsoluteY = 0;
    this->contentColor = CURSESPP_DEFAULT_CONTENT_COLOR;
    this->frameColor = CURSESPP_DEFAULT_FRAME_COLOR;
    this->focusedContentColor = CURSESPP_DEFAULT_CONTENT_COLOR;
    this->focusedFrameColor = CURSESPP_FOCUSED_FRAME_COLOR;
    this->drawFrame = true;
    this->isVisible = false;
    this->isFocused = false;
    this->isDirty = true;
    this->focusOrder = -1;
    this->id = NEXT_ID++;
    this->badBounds = false;
}

Window::~Window() {
    messageQueue.Remove(this);
    this->Destroy();
}

int Window::GetId() const {
    return this->id;
}

void Window::ProcessMessage(musik::core::runtime::IMessage &message) {

}

bool Window::IsVisible() {
    return this->isVisible;
}

bool Window::IsFocused() {
    return this->isFocused;
}

void Window::BringToTop() {
    if (this->framePanel) {
        top_panel(this->framePanel);

        if (this->contentPanel != this->framePanel) {
            top_panel(this->contentPanel);
        }

        ::top = this;
    }
}

bool Window::IsTop() {
    return (::top == this);
}

void Window::SendToBottom() {
    if (this->framePanel) {
        bottom_panel(this->contentPanel);

        if (this->contentPanel != this->framePanel) {
            bottom_panel(this->framePanel);
        }
    }
}

void Window::PostMessage(int messageType, int64 user1, int64 user2, int64 delay) {
    messageQueue.Post(
        Message::Create(
            this,
            messageType,
            user1,
            user2),
        delay);
}

void Window::DebounceMessage(int messageType, int64 user1, int64 user2, int64 delay) {
    messageQueue.Debounce(
        Message::Create(
            this,
            messageType,
            user1,
            user2),
        delay);
}

void Window::RemoveMessage(int messageType) {
    messageQueue.Remove(this, messageType);
}

void Window::SetParent(IWindow* parent) {
    if (this->parent != parent) {
        IWindowGroup* group = dynamic_cast<IWindowGroup*>(this->parent);

        this->parent = parent;

        if (this->frame) {
            this->Hide();
            this->Show();
        }
    }
}

void Window::RecreateForUpdatedDimensions() {
    bool hasFrame = !!this->frame;
    if (hasFrame || this->isVisible) {
        this->Recreate();

        if (!hasFrame) {
            this->OnVisibilityChanged(true);
        }
    }

    this->OnDimensionsChanged();
}

void Window::MoveAndResize(int x, int y, int width, int height) {
    bool sizeChanged = this->width != width || this->height != height;

    this->x = x;
    this->y = y;
    int absX = this->GetAbsoluteX();
    int absY = this->GetAbsoluteY();

    bool positionChanged =
        absX != this->lastAbsoluteX ||
        absY != this->lastAbsoluteY;

    if (sizeChanged || positionChanged) {
        this->lastAbsoluteX = absX;
        this->lastAbsoluteY = absY;
        this->width = width;
        this->height = height;
        this->RecreateForUpdatedDimensions();
    }
}

void Window::SetSize(int width, int height) {
    if (this->width != width || this->height != height) {
        this->width = width;
        this->height = height;
        this->RecreateForUpdatedDimensions();
    }
}

void Window::SetPosition(int x, int y) {
    this->x = x;
    this->y = y;
    int absX = this->GetAbsoluteX();
    int absY = this->GetAbsoluteY();

    if (absX != this->lastAbsoluteX || absY != this->lastAbsoluteY) {
        this->lastAbsoluteX = absX;
        this->lastAbsoluteY = absY;
        this->RecreateForUpdatedDimensions();
    }
}

void Window::OnDimensionsChanged() {
    this->Invalidate();
}

void Window::OnVisibilityChanged(bool visible) {
    /* for subclass use */
}

void Window::OnFocusChanged(bool focused) {
    /* for subclass use */
}

void Window::OnRedraw() {
    /* for subclass use */
}

void Window::Redraw() {
    this->isDirty = true;

    if (this->IsVisible()) {
        if (this->parent && !this->parent->IsVisible()) {
            return;
        }

        if (!this->frame) {
            this->Create();
        }

        if (this->frame) {
            this->OnRedraw();
            this->Invalidate();
            this->isDirty = false;
            return;
        }
    }
}

int Window::GetWidth() const {
    return this->width;
}

int Window::GetHeight() const {
    return this->height;
}

int Window::GetContentHeight() const {
    if (!this->drawFrame) {
        return this->height;
    }

    return this->height ? this->height - 2 : 0;
}

int Window::GetContentWidth() const {
    if (!this->drawFrame) {
        return this->width;
    }

    return this->width ? this->width - 2 : 0;
}

int Window::GetX() const {
    return this->x;
}

int Window::GetY() const {
    return this->y;
}

void Window::SetContentColor(int64 color) {
    this->contentColor = (color == CURSESPP_DEFAULT_COLOR)
        ? CURSESPP_DEFAULT_CONTENT_COLOR : color;

    this->RepaintBackground();
}

void Window::SetFocusedContentColor(int64 color) {
    this->focusedContentColor = (color == CURSESPP_DEFAULT_COLOR)
        ? CURSESPP_DEFAULT_CONTENT_COLOR : color;

    this->RepaintBackground();
}

void Window::SetFrameColor(int64 color) {
    this->frameColor = (color == CURSESPP_DEFAULT_COLOR)
        ? CURSESPP_DEFAULT_FRAME_COLOR : color;

    this->RepaintBackground();
}

void Window::SetFocusedFrameColor(int64 color) {
    this->focusedFrameColor = (color == CURSESPP_DEFAULT_COLOR)
        ? CURSESPP_FOCUSED_FRAME_COLOR : color;

    this->RepaintBackground();
}

void Window::RepaintBackground() {
    if (this->drawFrame &&
        this->frameColor != CURSESPP_DEFAULT_COLOR &&
        this->frame &&
        this->content != this->frame)
    {
        wbkgd(this->frame, COLOR_PAIR(IsFocused()
            ? this->focusedFrameColor : this->frameColor));
    }

    if (this->content) {
        wbkgd(this->content, COLOR_PAIR(IsFocused()
            ? this->focusedContentColor : this->contentColor));
    }

    this->Invalidate();
}

WINDOW* Window::GetContent() const {
    return this->content;
}

WINDOW* Window::GetFrame() const {
    return this->frame;
}

int Window::GetFocusOrder() {
    return this->focusOrder;
}

void Window::SetFocusOrder(int order) {
    this->focusOrder = order;
}

void Window::Show() {
    if (parent && !parent->IsVisible()) {
        /* remember that someone tried to make us visible, but don't do
        anything because we could corrupt the display */
        this->isVisible = true;
        return;
    }

    if (this->badBounds) {
        if (!this->CheckForBoundsError()) {
            this->Recreate();
            this->badBounds = false;
        }

        this->isVisible = true;
        return;
    }

    if (this->framePanel) {
        if (!this->isVisible) {
            show_panel(this->framePanel);

            if (this->framePanel != this->contentPanel) {
                show_panel(this->contentPanel);
            }

            this->isVisible = true;
            drawPending = true;

            this->OnVisibilityChanged(true);
        }
    }
    else {
        this->Create();
    }

    if (this->isDirty) {
        this->Redraw();
    }
}

void Window::Recreate() {
    this->Destroy();
    this->Create();
}

void Window::OnParentVisibilityChanged(bool visible) {
    if (!visible && this->isVisible) {
        if (this->framePanel) {
            this->Destroy();
        }

        this->OnVisibilityChanged(false);
    }
    else if (visible && this->isVisible) {
        if (this->framePanel) {
            this->Redraw();
        }
        else {
            this->Recreate();
        }

        this->OnVisibilityChanged(true);
    }
}

bool Window::CheckForBoundsError() {
    if (this->parent && ((Window *)this->parent)->CheckForBoundsError()) {
        return true;
    }

    int screenCy = Screen::GetHeight();
    int screenCx = Screen::GetWidth();

    int cx = this->GetWidth();
    int cy = this->GetHeight();

    if (cx <= 0 || cy <= 0) {
        return true;
    }

    if (cx > screenCx || cy > screenCy) {
        return true;
    }

    if (this->drawFrame && (cx < 3 || cy < 3)) {
        /* if a frame is visible, the minimum x and y dimensions are
        2 cells -- the left+right (and top+bottom) border, plus the
        content area (at least 1 cell) */
        return true;
    }

    if (!this->parent) {
        return false;
    }

    if (this->width > this->parent->GetContentWidth() ||
        this->height > this->parent->GetContentHeight())
    {
        return true;
    }

    return false;
}

int Window::GetAbsoluteX() const {
    return this->parent ? (this->x + parent->GetAbsoluteX()) : this->x;
}

int Window::GetAbsoluteY() const {
    return this->parent ? (this->y + parent->GetAbsoluteY()) : this->y;
}

void Window::Create() {
    assert(this->frame == nullptr);
    assert(this->content == nullptr);
    assert(this->framePanel == nullptr);
    assert(this->contentPanel == nullptr);

    bool hadBadBounds = this->badBounds;
    this->badBounds = this->CheckForBoundsError();

    if (this->badBounds) {
        this->Destroy();
        return;
    }

    /* else we have valid bounds. this->x and this->y are specified in
    relative space; find their absolute offset based on our parent. */

    int absoluteXOffset = 0;
    int absoluteYOffset = 0;

    if (this->parent) {
        absoluteXOffset = this->parent->GetAbsoluteX();
        absoluteYOffset = this->parent->GetAbsoluteY();

        if (parent->IsFrameVisible()) {
            absoluteXOffset += 1;
            absoluteYOffset += 1;
        }
    }

    this->frame = newwin(
        this->height,
        this->width,
        absoluteYOffset + this->y,
        absoluteXOffset + this->x);

    if (this->frame) { /* can fail if the screen size is too small. */
        /* resolve our current colors colors */

        bool focused = this->IsFocused();

        int64 currentFrameColor = focused
            ? this->focusedFrameColor : this->frameColor;

        int64 currentContentColor = focused
            ? this->focusedContentColor : this->contentColor;

        /* create the corresponding panel. required for z-ordering. */

        this->framePanel = new_panel(this->frame);

        /* if we were asked not to draw a frame, we'll set the frame equal to
        the content view, and use the content views colors*/

        if (!this->drawFrame) {
            this->content = this->frame;

            if (currentContentColor != CURSESPP_DEFAULT_COLOR) {
                wbkgd(this->frame, COLOR_PAIR(currentContentColor));
            }
        }

        /* otherwise we'll draw a box around the frame, and create a content
        sub-window inside */

        else {
            box(this->frame, 0, 0);

            this->content = newwin(
                this->height - 2,
                this->width - 2,
                absoluteYOffset + this->y + 1,
                absoluteXOffset + this->x + 1);

            if (!this->content) {
                /* should never happen. if there's enough room for this->frame,
                there's enough room for this->content */
                this->Destroy();
                return;
            }

            this->contentPanel = new_panel(this->content);

            if (currentFrameColor != CURSESPP_DEFAULT_COLOR) {
                wbkgd(this->frame, COLOR_PAIR(currentFrameColor));
            }

            if (currentContentColor != CURSESPP_DEFAULT_COLOR) {
                wbkgd(this->content, COLOR_PAIR(currentContentColor));
            }
        }

        this->Show();

        if (hadBadBounds && this->isVisible) {
            this->OnVisibilityChanged(true);
        }
    }
}

void Window::Hide() {
    if (this->frame) {
        if (this->isVisible) {
            this->Destroy();
            this->isVisible = false;
            this->OnVisibilityChanged(false);
        }
    }
    else {
        this->isVisible = false;
    }
}

void Window::Destroy() {
    if (this->frame) {
        del_panel(this->framePanel);
        delwin(this->frame);

        if (this->content != this->frame) {
            del_panel(this->contentPanel);
            delwin(this->content);
        }
    }

    this->framePanel = this->contentPanel = 0;
    this->content = this->frame = 0;
    this->isDirty = true;
}

void Window::SetFrameVisible(bool enabled) {
    if (enabled != this->drawFrame) {
        this->drawFrame = enabled;

        if (this->frame || this->content) {
            this->Destroy();
            this->Show();
        }
    }
}

bool Window::IsFrameVisible() {
    return this->drawFrame;
}

IWindow* Window::GetParent() const {
    return this->parent;
}

void Window::Clear() {
    werase(this->content);
    wmove(this->content, 0, 0);

    bool focused = this->IsFocused();
    int64 contentColor = isFocused ? this->focusedContentColor : this->contentColor;
    int64 frameColor = isFocused ? this->focusedFrameColor : this->frameColor;

    if (this->content == this->frame) {
        wbkgd(this->frame, COLOR_PAIR(contentColor));
    }
    else {
        wbkgd(this->frame, COLOR_PAIR(frameColor));
        wbkgd(this->content, COLOR_PAIR(contentColor));
    }
}

void Window::Invalidate() {
    if (this->isVisible) {
        if (this->frame) {
            drawPending = true;
        }
    }
}

void Window::Focus() {
    if (!this->isFocused) {
        this->isFocused = true;
        this->isDirty = true;
        this->OnFocusChanged(true);
        this->RepaintBackground();
        this->Redraw();
    }
}

void Window::Blur() {
    if (this->isFocused) {
        this->isFocused = false;
        this->isDirty = true;
        this->OnFocusChanged(false);
        this->RepaintBackground();
        this->Redraw();
    }
}
