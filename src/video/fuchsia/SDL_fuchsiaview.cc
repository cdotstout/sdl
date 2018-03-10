/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_FUCHSIA

#include "SDL_fuchsiaview.h"

extern "C" {
#include "../../events/SDL_events_c.h"
}

#include "garnet/public/lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/scenic/client/resources.h"

FuchsiaView::FuchsiaView(
    mozart::ViewManagerPtr view_manager,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    std::function<void()> resize_callback,
    std::function<void(bool relative, float x, float y, uint32_t buttons)> mouse_event_callback)
    : BaseView(std::move(view_manager), std::move(view_owner_request), "sdl"),
      resize_callback_(resize_callback), mouse_event_callback_(mouse_event_callback),
      pane_node_(session())
{
}

void
FuchsiaView::OnSceneInvalidated(ui_mozart::PresentationInfoPtr presentation_info)
{
    if (size_.Equals(logical_size()))
        return;

    size_ = logical_size();

    scenic_lib::Rectangle pane_shape(session(), logical_size().width, logical_size().height);
    scenic_lib::Material pane_material(session());

    pane_node_.SetShape(pane_shape);
    pane_node_.SetMaterial(pane_material);
    pane_node_.SetTranslation(logical_size().width * 0.5, logical_size().height * 0.5, 0);
    parent_node().AddChild(pane_node_);

    zx::channel endpoint0;
    zx::channel endpoint1;
    zx::channel::create(0, &endpoint0, &endpoint1);

    uint32_t image_pipe_id = session()->AllocResourceId();
    session()->Enqueue(scenic_lib::NewCreateImagePipeOp(
        image_pipe_id, f1dl::InterfaceRequest<scenic::ImagePipe>(std::move(endpoint1))));
    pane_material.SetTexture(image_pipe_id);
    session()->ReleaseResource(image_pipe_id);

    image_pipe_handle_ =
        f1dl::InterfaceHandle<scenic::ImagePipe>(std::move(endpoint0)).TakeChannel().release();

    resize_callback_();
}

bool
FuchsiaView::OnInputEvent(mozart::InputEventPtr event)
{
    if (event->is_keyboard()) {
        const mozart::KeyboardEventPtr &key_event = event->get_keyboard();
        switch (key_event->phase) {
            case mozart::KeyboardEvent::Phase::PRESSED:
                SDL_SendKeyboardKey(SDL_PRESSED, static_cast<SDL_Scancode>(key_event->hid_usage));
                break;
            case mozart::KeyboardEvent::Phase::RELEASED:
            case mozart::KeyboardEvent::Phase::CANCELLED:
                SDL_SendKeyboardKey(SDL_RELEASED, static_cast<SDL_Scancode>(key_event->hid_usage));
                break;
            case mozart::KeyboardEvent::Phase::REPEAT:
                break; // ignore
        }
        return true;
    } else if (event->is_pointer()) {
        const mozart::PointerEventPtr &ptr_event = event->get_pointer();
        // printf("ptr event type %d phase %d (mode %d)\n", ptr_event->type, ptr_event->phase,
        // relative_pointer_mode_);
        if (relative_pointer_mode_) {
            if (ptr_event->type == mozart::PointerEvent::Type::TOUCH &&
                ptr_event->phase != mozart::PointerEvent::Phase::MOVE) {
                ptr_x_ = ptr_event->x;
                ptr_y_ = ptr_event->y;
            } else {
                float dx, dy;
                UpdatePointer(ptr_event->x, ptr_event->y, &dx, &dy);
                mouse_event_callback_(true, dx, dy, ptr_event->buttons);
            }
        } else {
            mouse_event_callback_(false, ptr_event->x, ptr_event->y, ptr_event->buttons);
        }
        return true;
    }
    return false;
}

#endif /* SDL_VIDEO_DRIVER_FUCHSIA */

/* vi: set ts=4 sw=4 expandtab: */
