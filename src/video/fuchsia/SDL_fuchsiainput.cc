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
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
}

#include "SDL_fuchsiainput.h"
#include "SDL_fuchsiautil.h"

// From fuchsia sysroot
#include <hid/hid.h>
#include <zircon/device/input.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

class InputDevice : public InputDeviceBase {
public:
    ~InputDevice() override
    {
        close(fd_);
    }

    void
    Pump() override
    {
        ssize_t ret = read(fd_, report_.data(), report_.size());
        if (ret < 0)
            return;
        ParseReport(report_.data(), ret);
    }

protected:
    // Takes ownership over arguments.
    InputDevice(int fd, unsigned int max_report_size) : fd_(fd), report_(max_report_size)
    {
    }

    virtual void
    ParseReport(uint8_t *report, size_t len) = 0;

    int fd_;
    std::vector<uint8_t> report_;
};

class KeyboardDevice : public InputDevice {
public:
    KeyboardDevice(int fd, unsigned int max_report_size) : InputDevice(fd, max_report_size)
    {
    }

protected:
    void
    ParseReport(uint8_t *report, size_t len) override
    {
        hid_keys_t current_key_state;

        hid_kbd_parse_report(report, &current_key_state);

        hid_keys_t pressed_key_state, released_key_state;
        hid_kbd_pressed_keys(&previous_key_state_, &current_key_state, &pressed_key_state);
        hid_kbd_released_keys(&previous_key_state_, &current_key_state, &released_key_state);

        uint8_t keycode;
        hid_for_every_key(&released_key_state, keycode)
        {
            SDL_SendKeyboardKey(SDL_RELEASED, static_cast<SDL_Scancode>(keycode));
        }
        hid_for_every_key(&pressed_key_state, keycode)
        {
            SDL_SendKeyboardKey(SDL_PRESSED, static_cast<SDL_Scancode>(keycode));
        }

        memcpy(&previous_key_state_, &current_key_state, sizeof(hid_keys_t));
    }

private:
    hid_keys_t previous_key_state_{};
};

class MouseDevice : public InputDevice {
public:
    MouseDevice(int fd, unsigned int max_report_size,
                std::function<void(boot_mouse_report_t *)> callback)
        : InputDevice(fd, max_report_size), callback_(callback)
    {
    }

protected:
    void
    ParseReport(uint8_t *report, size_t len) override
    {
        if (callback_)
            callback_(reinterpret_cast<boot_mouse_report_t *>(report));
    }

private:
    std::function<void(boot_mouse_report_t *)> callback_;
};

std::unique_ptr<InputManager>
InputManager::Create()
{
    const char DEV_INPUT[] = "/dev/class/input";
    DIR *dir = opendir(DEV_INPUT);
    if (!dir)
        return DRET(nullptr, "Error opening DEV_INPUT");

    auto input_manager = std::unique_ptr<InputManager>(new InputManager());

    struct dirent *de;
    while ((de = readdir(dir))) {
        // extra +1 ensures space for null termination
        char name[sizeof(DEV_INPUT) + sizeof('/') + (NAME_MAX + 1) + 1];
        snprintf(name, sizeof(name), "%s/%s", DEV_INPUT, de->d_name);

        struct stat path_stat;
        stat(name, &path_stat);
        if (S_ISDIR(path_stat.st_mode))
            continue;

        ScopedFd fd(open(name, O_RDONLY | O_NONBLOCK));
        if (fd.get() < 0)
            continue;

        int proto;
        ssize_t rc = ioctl_input_get_protocol(fd.get(), &proto);
        if (rc < 0)
            continue;

        input_report_size_t max_report_len;
        rc = ioctl_input_get_max_reportsize(fd.get(), &max_report_len);
        if (rc < 0)
            continue;

        if (proto == INPUT_PROTO_KBD) {
            printf("Found keyboard: %s\n", name);
            input_manager->AddDevice(
                std::unique_ptr<KeyboardDevice>(new KeyboardDevice(fd.release(), max_report_len)));
        } else if (proto == INPUT_PROTO_MOUSE) {
            printf("Found mouse: %s\n", name);
            auto owner = input_manager.get();
            auto callback = [owner](boot_mouse_report_t *report) {
                if (owner->window_) {
                    SDL_SendMouseMotion(owner->window_, 0, 1, report->rel_x, report->rel_y);
                    SDL_SendMouseButton(owner->window_, 0,
                                        (report->buttons & 1) ? SDL_PRESSED : SDL_RELEASED,
                                        SDL_BUTTON_LEFT);
                    SDL_SendMouseButton(owner->window_, 0,
                                        (report->buttons & (1 << 1)) ? SDL_PRESSED : SDL_RELEASED,
                                        SDL_BUTTON_RIGHT);
                }
            };
            input_manager->AddDevice(std::unique_ptr<MouseDevice>(
                new MouseDevice(fd.release(), max_report_len, callback)));
        } else {
            printf("Unhandled input device: %s proto %d\n", name, proto);
        }
    }

    return input_manager;
}

void
InputManager::Pump()
{
    for (auto &device : input_devices_) {
        device->Pump();
    }
}

#endif /* SDL_VIDEO_DRIVER_FUCHSIA */

/* vi: set ts=4 sw=4 expandtab: */
