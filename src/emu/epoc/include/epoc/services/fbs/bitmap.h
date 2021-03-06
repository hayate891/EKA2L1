#pragma once

#include <epoc/loader/mbm.h>
#include <epoc/utils/uid.h>
#include <epoc/ptr.h>

namespace eka2l1::epoc {
    struct bitwise_bitmap {
        enum settings_flag {
            dirty_bitmap = 0x00010000,
            violate_bitmap = 0x00020000
        };

        uid                uid_;
        std::uint32_t      flags_;
        eka2l1::ptr<void>  allocator_;
        eka2l1::ptr<void>  pile_;
        int                byte_width_;
        loader::sbm_header header_;
        int                spare1_;
        int                data_offset_;
        bool               compressed_in_ram_;
    };

    constexpr epoc::uid bitwise_bitmap_uid = 0x10000040;
}
