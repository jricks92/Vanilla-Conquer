//
// Touch input for the SDL2 backend.
//
// Raw SDL finger events are translated into the same synthetic mouse events
// the rest of the engine already understands, using a deferred gesture state
// machine: nothing is emitted on finger down, the gesture decides. The scheme
// is adapted from the PeonPad iPad RTS port:
//
//   one finger tap    -> left click at the touch point
//   one finger drag   -> left-button drag anchored at the original touch point
//   two finger tap    -> right click at the leftmost finger (deselect)
//   three finger drag -> map scroll via the analog scroll channel gamepads use
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

#include "wwkeyboard_sdl2.h"
#include "video.h"
#include <cmath>
#include <SDL.h>

namespace
{
    float Touch_Distance_Points(float nx1, float ny1, float nx2, float ny2)
    {
        int win_w, win_h;
        Get_Video_Window_Size(win_w, win_h);

        float dx = (nx2 - nx1) * win_w;
        float dy = (ny2 - ny1) * win_h;
        return std::sqrt(dx * dx + dy * dy);
    }
} // namespace

int WWKeyboardClassSDL2::Find_Touch(SDL_FingerID id) const
{
    for (int i = 0; i < TouchCount; ++i) {
        if (Touches[i].Id == id) {
            return i;
        }
    }
    return -1;
}

bool WWKeyboardClassSDL2::Touch_Centroid(float& nx, float& ny) const
{
    if (TouchCount == 0) {
        return false;
    }

    float sx = 0.0f, sy = 0.0f;
    for (int i = 0; i < TouchCount; ++i) {
        sx += Touches[i].CurX;
        sy += Touches[i].CurY;
    }
    nx = sx / TouchCount;
    ny = sy / TouchCount;
    return true;
}

void WWKeyboardClassSDL2::Handle_Touch_Event(const SDL_Event& event)
{
    /*
    ** Only real touchscreens; trackpads report as indirect touch devices and
    ** must keep acting as mice.
    */
    if (SDL_GetTouchDeviceType(event.tfinger.touchId) != SDL_TOUCH_DEVICE_DIRECT) {
        return;
    }

    switch (event.type) {
    case SDL_FINGERDOWN: {
        ++RawFingerCount;

        if (TouchCount < MAX_TOUCH) {
            TouchPoint& tp = Touches[TouchCount];
            tp.Id = event.tfinger.fingerId;
            tp.DownX = tp.CurX = event.tfinger.x;
            tp.DownY = tp.CurY = event.tfinger.y;
            ++TouchCount;
        }

        /*
        ** Transition purely on finger count. Adding a finger always abandons a
        ** lower-finger gesture before it commits: since nothing is emitted
        ** until a gesture identifies itself, an abandoned pending tap leaves no
        ** stray click behind.
        */
        if (RawFingerCount == 1) {
            TouchState = TOUCH_PENDING;
        } else if (RawFingerCount == 2) {
            TouchState = TOUCH_TWO;
        } else if (RawFingerCount == 3) {
            TouchState = TOUCH_PANNING;
            float cx, cy;
            Touch_Centroid(cx, cy);
            PanPrevNX = cx;
            PanPrevNY = cy;
            PanLastMoveTicks = SDL_GetTicks();
        } else {
            // Four or more fingers: not a supported gesture.
            TouchState = TOUCH_CANCELLED;
        }
        break;
    }

    case SDL_FINGERMOTION: {
        int idx = Find_Touch(event.tfinger.fingerId);
        if (idx >= 0) {
            Touches[idx].CurX = event.tfinger.x;
            Touches[idx].CurY = event.tfinger.y;
        }

        switch (TouchState) {
        case TOUCH_PENDING: {
            if (idx < 0) {
                break;
            }
            if (Touch_Distance_Points(Touches[idx].DownX, Touches[idx].DownY, Touches[idx].CurX, Touches[idx].CurY)
                > TOUCH_DRAG_THRESHOLD_PT) {
                /*
                ** Commit to a drag: press the left button at the original
                ** touch point so selection boxes anchor where the finger first
                ** landed, then move to the current position.
                */
                int gx, gy;
                Video_Touch_To_Game(Touches[idx].DownX, Touches[idx].DownY, gx, gy);
                Set_Video_Mouse(gx, gy);
                Put_Mouse_Message(VK_LBUTTON, gx, gy, false);

                Video_Touch_To_Game(Touches[idx].CurX, Touches[idx].CurY, gx, gy);
                Set_Video_Mouse(gx, gy);

                TouchState = TOUCH_DRAGGING;
            }
            break;
        }

        case TOUCH_DRAGGING: {
            if (idx >= 0) {
                int gx, gy;
                Video_Touch_To_Game(Touches[idx].CurX, Touches[idx].CurY, gx, gy);
                Set_Video_Mouse(gx, gy);
            }
            break;
        }

        case TOUCH_TWO: {
            /*
            ** Movement past a small tolerance cancels the pending right-click
            ** tap (it was a stray two-finger slide, not a tap).
            */
            if (idx >= 0
                && Touch_Distance_Points(Touches[idx].DownX, Touches[idx].DownY, Touches[idx].CurX, Touches[idx].CurY)
                       > TOUCH_TWO_CANCEL_PT) {
                TouchState = TOUCH_CANCELLED;
            }
            break;
        }

        case TOUCH_PANNING: {
            float cx, cy;
            if (!Touch_Centroid(cx, cy)) {
                break;
            }

            if (Touch_Distance_Points(PanPrevNX, PanPrevNY, cx, cy) >= TOUCH_PAN_STEP_PT) {
                /*
                ** Dragging moves the viewport the way the fingers travel
                ** (content follows the fingers).
                */
                float dx = cx - PanPrevNX;
                float dy = cy - PanPrevNY;

                int win_w, win_h;
                Get_Video_Window_Size(win_w, win_h);
                float adx = std::fabs(dx) * win_w;
                float ady = std::fabs(dy) * win_h;

                ScrollDirType dir_x = SDIR_NONE;
                ScrollDirType dir_y = SDIR_NONE;

                if (adx > 0.4f * ady) {
                    dir_x = dx > 0.0f ? SDIR_W : SDIR_E;
                }
                if (ady > 0.4f * adx) {
                    dir_y = dy > 0.0f ? SDIR_N : SDIR_S;
                }

                if (dir_x == SDIR_E) {
                    ScrollDirection = dir_y == SDIR_N ? SDIR_NE : (dir_y == SDIR_S ? SDIR_SE : SDIR_E);
                } else if (dir_x == SDIR_W) {
                    ScrollDirection = dir_y == SDIR_N ? SDIR_NW : (dir_y == SDIR_S ? SDIR_SW : SDIR_W);
                } else if (dir_y != SDIR_NONE) {
                    ScrollDirection = dir_y;
                }

                AnalogScrollActive = true;
                PanPrevNX = cx;
                PanPrevNY = cy;
                PanLastMoveTicks = SDL_GetTicks();
            }
            break;
        }

        default:
            break;
        }
        break;
    }

    case SDL_FINGERUP: {
        if (RawFingerCount > 0) {
            --RawFingerCount;
        }

        switch (TouchState) {
        case TOUCH_PENDING: {
            /*
            ** A quick single-finger lift is a tap: cursor motion plus both
            ** button edges, all at the original touch point so dense UI
            ** buttons are hit reliably.
            */
            int idx = Find_Touch(event.tfinger.fingerId);
            float nx = idx >= 0 ? Touches[idx].DownX : event.tfinger.x;
            float ny = idx >= 0 ? Touches[idx].DownY : event.tfinger.y;

            int gx, gy;
            Video_Touch_To_Game(nx, ny, gx, gy);
            Set_Video_Mouse(gx, gy);
            Put_Mouse_Message(VK_LBUTTON, gx, gy, false);
            Put_Mouse_Message(VK_LBUTTON, gx, gy, true);

            /*
            ** If a text field wants input, a tap re-raises the software
            ** keyboard in case the user dismissed it with the on-screen hide
            ** key. Harmless (no-op) everywhere else.
            */
            Reraise_Virtual_Keyboard();

            TouchState = TOUCH_CANCELLED;
            break;
        }

        case TOUCH_DRAGGING: {
            int idx = Find_Touch(event.tfinger.fingerId);
            float nx = idx >= 0 ? Touches[idx].CurX : event.tfinger.x;
            float ny = idx >= 0 ? Touches[idx].CurY : event.tfinger.y;

            int gx, gy;
            Video_Touch_To_Game(nx, ny, gx, gy);
            Set_Video_Mouse(gx, gy);
            Put_Mouse_Message(VK_LBUTTON, gx, gy, true);
            TouchState = TOUCH_CANCELLED;
            break;
        }

        case TOUCH_TWO: {
            /*
            ** Lifting a finger from a stationary two-finger touch is a
            ** right-click. PeonPad places it at the leftmost finger; do the
            ** same so the command lands predictably.
            */
            int left = 0;
            for (int i = 1; i < TouchCount; ++i) {
                if (Touches[i].CurX < Touches[left].CurX) {
                    left = i;
                }
            }

            int gx, gy;
            Video_Touch_To_Game(Touches[left].CurX, Touches[left].CurY, gx, gy);
            Set_Video_Mouse(gx, gy);
            Put_Mouse_Message(VK_RBUTTON, gx, gy, false);
            Put_Mouse_Message(VK_RBUTTON, gx, gy, true);
            TouchState = TOUCH_CANCELLED;
            break;
        }

        case TOUCH_PANNING:
            AnalogScrollActive = false;
            ScrollDirection = SDIR_NONE;
            TouchState = TOUCH_CANCELLED;
            break;

        default:
            break;
        }

        /*
        ** Drop the lifted finger from the tracking table.
        */
        int idx = Find_Touch(event.tfinger.fingerId);
        if (idx >= 0) {
            for (int i = idx; i < TouchCount - 1; ++i) {
                Touches[i] = Touches[i + 1];
            }
            --TouchCount;
        }

        /*
        ** Once every finger is up, reset so the next touch starts a fresh
        ** gesture.
        */
        if (RawFingerCount <= 0) {
            RawFingerCount = 0;
            TouchCount = 0;
            AnalogScrollActive = false;
            ScrollDirection = SDIR_NONE;
            TouchState = TOUCH_IDLE;
        }
        break;
    }

    default:
        break;
    }
}

void WWKeyboardClassSDL2::Process_Touch()
{
    /*
    ** Stop scrolling when the pan stalls without finger movement.
    */
    if (TouchState == TOUCH_PANNING && AnalogScrollActive && SDL_GetTicks() - PanLastMoveTicks > TOUCH_PAN_IDLE_MS) {
        AnalogScrollActive = false;
    }
}
