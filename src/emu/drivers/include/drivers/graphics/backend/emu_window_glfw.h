/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#ifdef WIN32
#include <Windows.h>
#endif

#include <common/vecx.h>
#include <drivers/graphics/emu_window.h>

#include <GLFW/glfw3.h>

#include <any>
#include <functional>

namespace eka2l1 {
    namespace drivers {
        class emu_window_glfw3 : public emu_window {
            GLFWwindow *emu_win;
            vec2 emu_screen_size;

        public:
            bool get_mouse_button_hold(const int mouse_btt) override;

            void change_title(std::string new_title) override;

            void init(std::string title, vec2 size) override;
            void make_current() override;
            void done_current() override;
            void swap_buffer() override;
            void poll_events() override;

            bool should_quit() override;

            void shutdown() override;

            vec2 window_size() override;
            vec2 window_fb_size() override;
            vec2d get_mouse_pos() override;
        };
    }
}
