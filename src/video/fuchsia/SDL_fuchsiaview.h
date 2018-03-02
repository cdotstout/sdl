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

#include "SDL_video.h"
#include "lib/ui/view_framework/base_view.h"

class FuchsiaView : public mozart::BaseView {
public:
    FuchsiaView(mozart::ViewManagerPtr view_manager,
                f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                std::function<void()> resize_callback,
                std::function<void(float x, float y, uint32_t buttons)> mouse_event_callback);

    uint32_t
    width()
    {
        return logical_size().width;
    }
    uint32_t
    height()
    {
        return logical_size().height;
    }

    zx_handle_t
    GetImagePipeHandle()
    {
        return image_pipe_handle_;
    }

    void
    UpdatePointer(float x, float y, float *dx, float *dy)
    {
        if (ptr_x_ < 0) {
            *dx = *dy = 0;
        } else {
            *dx = x - ptr_x_;
            *dy = y - ptr_y_;
        }
        ptr_x_ = x;
        ptr_y_ = y;
    }

private:
    void
    OnSceneInvalidated(ui_mozart::PresentationInfoPtr presentation_info) override;

    bool
    OnInputEvent(mozart::InputEventPtr event) override;

    mozart::SizeF size_;
    std::function<void()> resize_callback_;
    std::function<void(float x, float y, uint32_t buttons)> mouse_event_callback_;
    scenic_lib::ShapeNode pane_node_;
    zx_handle_t image_pipe_handle_{};
    float ptr_x_ = -1;
    float ptr_y_ = -1;

    FXL_DISALLOW_COPY_AND_ASSIGN(FuchsiaView);
};

#endif /* SDL_VIDEO_DRIVER_FUCHSIA */
