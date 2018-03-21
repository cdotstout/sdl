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

#include "SDL_timer.h"
#include "SDL_video.h"

extern "C" {
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include "../src/events/SDL_windowevents_c.h"
}

#include "SDL_fuchsiainput.h"
#include "SDL_fuchsiautil.h"
#include "SDL_fuchsiaview.h"
#include "SDL_fuchsiavulkan.h"

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "magma.h"

#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h> // for close
#include <vector>

#define FUCHSIA_VID_DRIVER_NAME "Fuchsia"

#define FUCHSIA_VIEW_ENABLED 1

class VideoData : public mozart::ViewProvider {
public:
    static std::unique_ptr<VideoData>
    Create(uint32_t devindex)
    {
        int size = std::snprintf(nullptr, 0, "/dev/class/display/%03d", devindex);
        std::vector<char> device_path(size + 1);
        std::snprintf(device_path.data(), device_path.size(), "/dev/class/display/%03d", devindex);

        int fd = open(device_path.data(), O_RDONLY);
        if (fd < 0)
            return DRET(nullptr, "open failed");

        return std::unique_ptr<VideoData>(new VideoData(fd));
    }

    VideoData(int fd) : fd_(fd)
    {
    }

    bool
    GetDisplaySize(uint32_t *width, uint32_t *height)
    {
        magma_display_size display_size;
        magma_status_t status = magma_display_get_size(fd_, &display_size);
        if (status != MAGMA_STATUS_OK)
            return false;
        *width = display_size.width;
        *height = display_size.height;
        return true;
    }

    ~VideoData()
    {
        if (application_context_)
            application_context_->outgoing_services()->RemoveService<mozart::ViewProvider>();
        close(fd_);
    }

    InputManager *
    input_manager()
    {
        return input_manager_.get();
    }

    void
    PumpMessageLoop()
    {
        loop_.RunUntilIdle();
    }

    void
    set_application_context(std::unique_ptr<component::ApplicationContext> context)
    {
        application_context_ = std::move(context);
    }

    component::ApplicationContext *
    application_context()
    {
        return application_context_.get();
    }

    bool
    CreateView();
    bool
    CreateInputManager();

    void
    SetWindow(SDL_Window *window)
    {
        window_ = window;
    }
    SDL_Window *
    window()
    {
        return window_;
    }

    FuchsiaView *
    view()
    {
        return window() ? reinterpret_cast<FuchsiaView *>(window()->driverdata) : nullptr;
    }

private:
    // |ViewProvider|
    void
    CreateView(f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
               f1dl::InterfaceRequest<component::ServiceProvider> view_services) override;

    int fd_;
    std::unique_ptr<InputManager> input_manager_;
    std::unique_ptr<component::ApplicationContext> application_context_;
    f1dl::BindingSet<mozart::ViewProvider> bindings_;
    fsl::MessageLoop loop_{};
    SDL_Window *window_{};
};

void
VideoData::CreateView(f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                      f1dl::InterfaceRequest<component::ServiceProvider> view_services)
{
    SDL_Window *window = this->window();

    auto resize_callback = [window]() {
        printf("Fuchsia resize callback\n");
        auto view = reinterpret_cast<FuchsiaView *>(window->driverdata);
        assert(view);
        if ((window->w != static_cast<int>(view->width())) ||
            (window->h != static_cast<int>(view->height()))) {
            SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED, view->width(), view->height());
        }
    };
    auto mouse_event_callback = [window](bool relative, float x, float y, uint32_t buttons) {
        SDL_SendMouseMotion(window, 0, relative, x, y);
        SDL_SendMouseButton(window, 0,
                            (buttons & mozart::kMousePrimaryButton) ? SDL_PRESSED : SDL_RELEASED,
                            SDL_BUTTON_LEFT);
        SDL_SendMouseButton(window, 0,
                            (buttons & mozart::kMouseSecondaryButton) ? SDL_PRESSED : SDL_RELEASED,
                            SDL_BUTTON_RIGHT);
    };

    assert(!window->driverdata);
    window->driverdata =
        new FuchsiaView(application_context_->ConnectToEnvironmentService<mozart::ViewManager>(),
                        std::move(view_owner_request), resize_callback, mouse_event_callback);
}

bool
VideoData::CreateView()
{
    application_context_->outgoing_services()->AddService<mozart::ViewProvider>(
        [this](f1dl::InterfaceRequest<mozart::ViewProvider> request) {
            printf("adding to bindings\n");
            bindings_.AddBinding(this, std::move(request));
        });

    uint32_t start = SDL_GetTicks();
    PumpMessageLoop();
    constexpr uint32_t kTimeMs = 5000;

    while (view() == nullptr) {
        if (SDL_GetTicks() - start > kTimeMs)
            return DRET(false, "timeout waiting for view creation");
        PumpMessageLoop();
        SDL_Delay(10);
    }

    while (view()->GetImagePipeHandle() == ZX_HANDLE_INVALID) {
        if (SDL_GetTicks() - start > kTimeMs)
            return DRET(false, "timeout waiting for image pipe");
        PumpMessageLoop();
        SDL_Delay(10);
    }

    // Fuchsia doesn't let you request a windows size, so update the initial window size
    // to avoid a practically-guaranteed resize.
    window()->w = window()->windowed.w = view()->width();
    window()->h = window()->windowed.h = view()->height();

    return true;
}

bool
VideoData::CreateInputManager()
{
    auto mouse_event_callback = [window = this->window()](float rel_x, float rel_y,
                                                          uint32_t buttons) {
        SDL_SendMouseMotion(window, 0, 1, rel_x, rel_y);
        SDL_SendMouseButton(window, 0, (buttons & 1) ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
        SDL_SendMouseButton(window, 0, (buttons & (1 << 1)) ? SDL_PRESSED : SDL_RELEASED,
                            SDL_BUTTON_RIGHT);
    };

    input_manager_ = InputManager::Create(mouse_event_callback);
    if (!input_manager_)
        return DRET(false, "InputManager::Create failed");

    return true;
}

int Fuchsia_VideoInit(_THIS)
{
    auto video_data = reinterpret_cast<VideoData *>(_this->driverdata);

    uint32_t width;
    uint32_t height;
    if (!video_data->GetDisplaySize(&width, &height))
        return DRET(-1, "GetDisplaySize failed");

    SDL_DisplayMode mode;
    mode.format = SDL_PIXELTYPE_PACKED32;
    mode.w = width;
    mode.h = height;
    mode.refresh_rate = 60;
    mode.driverdata = nullptr;

    if (SDL_AddBasicVideoDisplay(&mode) < 0)
        return DRET(-1, "SDL_AddBasicVideoDisplay failed");

    SDL_AddDisplayMode(&_this->displays[0], &mode);

#if FUCHSIA_VIEW_ENABLED
    video_data->set_application_context(component::ApplicationContext::CreateFromStartupInfo());
    if (!video_data->application_context())
        return DRET(-1, "Failed to get application context");
#endif

    return 0;
}

void Fuchsia_VideoQuit(_THIS)
{
}

void Fuchsia_PumpEvents(_THIS)
{
    auto video_data = reinterpret_cast<VideoData *>(_this->driverdata);
    video_data->PumpMessageLoop();
    if (video_data->input_manager())
        video_data->input_manager()->Pump();
}

static int
Fuchsia_CreateWindow(_THIS, SDL_Window *window)
{
    auto video_data = reinterpret_cast<VideoData *>(_this->driverdata);
    video_data->SetWindow(window);

#if FUCHSIA_VIEW_ENABLED
    if (!video_data->CreateView())
        return DRET(-1, "CreateView failed");
#else
    if (!video_data->CreateInputManager())
        return DRET(-1, "CreateInputManager failed");
#endif

    return 0;
}

static void
Fuchsia_DestroyWindow(_THIS, SDL_Window *window)
{
    reinterpret_cast<VideoData *>(_this->driverdata)->SetWindow(nullptr);
    delete reinterpret_cast<FuchsiaView *>(window->driverdata);
    window->driverdata = nullptr;
}

static void
Fuchsia_DeleteDevice(SDL_VideoDevice *device)
{
    delete reinterpret_cast<VideoData *>(device->driverdata);
    SDL_free(device);
}

static int
Fuchsia_SetRelativeMouseMode(SDL_bool enabled)
{
    auto video_data = reinterpret_cast<VideoData *>(SDL_GetVideoDevice()->driverdata);
    if (video_data->view()) {
        video_data->view()->SetRelativePointerMode(enabled);
        return 1;
    }
    return 0;
}

static SDL_VideoDevice *
Fuchsia_CreateDevice(int devindex)
{
    auto device = reinterpret_cast<SDL_VideoDevice *>(SDL_calloc(1, sizeof(SDL_VideoDevice)));
    if (!device) {
        SDL_OutOfMemory();
        return DRET(nullptr, "video device alloc failed");
    }

    auto video_data = VideoData::Create(devindex);
    if (!video_data)
        return DRET(nullptr, "VideoData::Create failed");

    device->driverdata = video_data.release();
    device->VideoInit = Fuchsia_VideoInit;
    device->VideoQuit = Fuchsia_VideoQuit;
    device->PumpEvents = Fuchsia_PumpEvents;
    device->free = Fuchsia_DeleteDevice;

#if SDL_VIDEO_VULKAN
    device->Vulkan_LoadLibrary = Fuchsia_Vulkan_LoadLibrary;
    device->Vulkan_UnloadLibrary = Fuchsia_Vulkan_UnloadLibrary;
    device->Vulkan_GetInstanceExtensions = Fuchsia_Vulkan_GetInstanceExtensions;
    device->Vulkan_CreateSurface = Fuchsia_Vulkan_CreateSurface;
#endif

    device->CreateSDLWindow = Fuchsia_CreateWindow;
    device->DestroyWindow = Fuchsia_DestroyWindow;

    SDL_Mouse *mouse = SDL_GetMouse();
    mouse->SetRelativeMouseMode = Fuchsia_SetRelativeMouseMode;

    return device;
}

static int
Fuchsia_Available(void)
{
    return 1;
}

VideoBootStrap Fuchsia_bootstrap = {FUCHSIA_VID_DRIVER_NAME, "SDL Fuchsia video driver",
                                    Fuchsia_Available, Fuchsia_CreateDevice};

#endif /* SDL_VIDEO_DRIVER_FUCHSIA */

/* vi: set ts=4 sw=4 expandtab: */
