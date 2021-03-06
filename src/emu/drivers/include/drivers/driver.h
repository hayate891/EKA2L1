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

#include <common/queue.h>
#include <drivers/itc.h>

#include <condition_variable>

namespace eka2l1::drivers {
    struct itc_context;

    struct driver_request {
        int opcode;
        int *status { nullptr };
        itc_context context;

        void finish(const int err_code) {
            status ? (*status = err_code) : 0;
        }
    };

    class driver {
    protected:
        friend class driver_client;
        threadsafe_cn_queue<driver_request> request_queue;

        std::condition_variable cond;

    public:
        virtual ~driver() {}
        virtual void process_requests() = 0;
    };
}
