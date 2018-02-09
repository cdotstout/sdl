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

#ifndef SDL_fuchsiautil_
#define SDL_fuchsiautil_

#include "../../SDL_internal.h"

#include <stdio.h>

#define DRET(t, msg) dret(t, __FILE__, __LINE__, msg)

template <class T>
T
dret(T ret, const char *file, int line, const char *msg)
{
    if (!static_cast<bool>(ret))
        printf("%s\n", msg);
    return ret;
}

template <int>
int
dret(int ret, const char *file, int line, const char *msg)
{
    if (ret)
        printf("%s\n", msg);
    return ret;
}

#endif /* SDL_fuchsiautil_ */
