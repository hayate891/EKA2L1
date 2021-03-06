/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project
 * (see bentokun.github.com/EKA2L1).
 * 
 * Initial contributor: pent0
 * Contributors:
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

#include <epoc/ptr.h>
#include <epoc/utils/des.h>

namespace eka2l1::epoc {
    struct open_font_metrics {
        std::int16_t design_height;
        std::int16_t ascent;
        std::int16_t descent;
        std::int16_t max_height;
        std::int16_t max_depth;
        std::int16_t max_width;
        std::int16_t baseline_correction;
        std::int16_t reserved;
    };

    struct open_font_face_attrib {
        enum {
            bold = 0x1,
            italic = 0x2,
            serif = 0x4,
            mono_width = 0x8
        };
        
        bufc_static<char16_t, 32> name;
        std::uint32_t coverage[4];
        std::int32_t style;
        std::int32_t reserved;
        bufc_static<char16_t, 32> fam_name;
        bufc_static<char16_t, 32> local_full_name;
        bufc_static<char16_t, 32> local_full_fam_name;
        std::int32_t min_size_in_pixels;
        std::int32_t reserved2;
    };

    struct typeface_info {
        epoc::bufc_static<char16_t, 0x18> name;
        std::uint32_t flags;

        enum {
            tf_propotional = 1,
            tf_serif = 2,
            tf_symbol = 4
        };
    };

    struct font_style {
        enum {
            italic = 0x1,
            bold = 0x2,
            super = 0x4,
            sub = 0x8
        };

        // 16 bit (high) = bitmap type, font effects = (12 bit) middule, 4 bits low for style
        std::uint32_t flags;
        eka2l1::ptr<void> reserved1;
        eka2l1::ptr<void> reserved2;
    };

    struct font_spec {
        typeface_info tf;
        std::int32_t height;
        font_style style;
    };

    enum {
        DEAD_VTABLE = 0xDEAD1AB
    };

    struct alg_style {
        int baseline_offsets_in_pixel;

        enum {
            bold = 0x1,
            italic = 0x2,
            mono = 0x4
        };

        std::uint8_t flags;
        std::uint8_t width_factor;
        std::uint8_t height_factor;
    };

    struct bitmapfont {
        eka2l1::ptr<void> vtable;

        font_spec spec_in_twips;
        alg_style algorithic_style;

        eka2l1::ptr<void> allocator;
        int fontbitmap_offset;

        // This was not used by Symbian's by default i think
        // Qt generally access this itself by hardcoding offset of this
        eka2l1::ptr<void> openfont;

        std::uint32_t reserved;
        std::uint32_t font_uid;
    };

    struct open_font {
        eka2l1::ptr<void> vtable;
        eka2l1::ptr<void> allocator;

        open_font_metrics metrics;
        eka2l1::ptr<void> sharper;

        std::int32_t font_captial_offset;
        std::int32_t font_max_ascent;
        std::int32_t font_standard_descent;
        std::int32_t font_line_gap;

        // Normally, i have seen code access this field to get font attrib, but that's the case if the font store
        // that creates this object is on the same process as the one who use this font.
        // FBS generally access the attrib by sending an opcode.
        std::int32_t file_offset;
        std::int32_t face_index_offset;
        std::int32_t glyph_cache_offset;
        std::int32_t session_cache_list_offset;

        eka2l1::ptr<void> reserved;
    };
}
