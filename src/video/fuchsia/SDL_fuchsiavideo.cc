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

extern "C" {
#include "../SDL_sysvideo.h"
}

#include "SDL_fuchsiautil.h"
#include "SDL_fuchsiavulkan.h"

#include <fcntl.h>
#include <magma.h>
#include <memory>
#include <string>
#include <unistd.h> // for close
#include <vector>

#define FUCHSIA_VID_DRIVER_NAME "Fuchsia"

class VideoData {
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
        close(fd_);
    }

private:
    int fd_;
};

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
    return 0;
}

void Fuchsia_VideoQuit(_THIS)
{
}

void Fuchsia_PumpEvents(_THIS)
{
}

static void
Fuchsia_DeleteDevice(SDL_VideoDevice *device)
{
    delete reinterpret_cast<VideoData *>(device->driverdata);
    SDL_free(device);
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
