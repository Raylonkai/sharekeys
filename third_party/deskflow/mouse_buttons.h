/*
 * Adapted from Deskflow MSWindowsDesks::fakeMouseButton.
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 * ShareKeys modifications: standalone encoder using ShareKeys event types.
 */
#pragma once
#include "core/input_event.h"
#include <windows.h>
namespace sharekeys::deskflow {
inline void encode_button(MouseButton button, bool press, MOUSEINPUT& input) {
    if (GetSystemMetrics(SM_SWAPBUTTON)) {
        if (button == MouseButton::left) button = MouseButton::right;
        else if (button == MouseButton::right) button = MouseButton::left;
    }
    switch (button) {
    case MouseButton::left: input.dwFlags = press ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case MouseButton::middle: input.dwFlags = press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case MouseButton::right: input.dwFlags = press ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case MouseButton::extra1:
        input.mouseData = XBUTTON1; input.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; break;
    case MouseButton::extra2:
        input.mouseData = XBUTTON2; input.dwFlags = press ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; break;
    }
}
}
