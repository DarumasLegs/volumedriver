// Copyright 2015 iNuron NV
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VD_BACKEND_GARBAGE_COLLECTOR_H_
#define VD_BACKEND_GARBAGE_COLLECTOR_H_

#include "Garbage.h"
#include "GarbageCollectorFwd.h"
#include "BackendParameters.h"

#include <future>
#include <boost/property_tree/ptree_fwd.hpp>

#include <youtils/Logging.h>
#include <youtils/ThreadPool.h>

#include <backend/BackendConnectionManager.h>

namespace backendtest
{

class GarbageCollectorTest;

}

namespace backend
{

struct GarbageCollectorThreadPoolTraits
{
    static const bool requeue_before_first_barrier_on_error = true;
    static const bool may_reorder = true;
    static const uint32_t max_number_of_threads = 256;
    static const char* component_name;

    using number_of_threads_type = initialized_params::PARAMETER_TYPE(bgc_threads);

    static uint64_t
    sleep_microseconds_if_queue_is_inactive()
    {
                return 1000000;
    }

    // exponential backoff based on errorcount
    static uint64_t
    wait_microseconds_before_retry_after_error(uint32_t errorcount)
    {
        switch (errorcount)
        {
#define SECS * 1000000

        case 0: return 0 SECS;
        case 1: return 1 SECS;
        case 2: return 2 SECS;
        case 3: return 4 SECS;
        case 4: return 8 SECS;
        case 5: return 15 SECS;
        case 6: return 30 SECS;
        case 7: return 60 SECS;
        case 8 : return 120 SECS;
        case 9: return 240 SECS;
        default : return 300 SECS;

#undef SECS
        }
    }

    static const std::string&
    default_producer_id()
    {
        static const std::string s;
        return s;
    }
};

class GarbageCollector
{
    friend class backendtest::GarbageCollectorTest;

public:
    GarbageCollector(backend::BackendConnectionManagerPtr,
                     const boost::property_tree::ptree& pt,
                     const RegisterComponent);

    ~GarbageCollector() = default;

    GarbageCollector(const GarbageCollector&) = delete;

    GarbageCollector&
    operator=(const GarbageCollector&) = delete;

    void
    queue(Garbage);

    std::future<bool>
    barrier(const backend::Namespace&);

    static const char*
    name();

    using ThreadPool = youtils::ThreadPool<std::string,
                                           GarbageCollectorThreadPoolTraits>;

private:
    DECLARE_LOGGER("GarbageCollector");

    backend::BackendConnectionManagerPtr cm_;
    ThreadPool thread_pool_;
};

}

#endif //!VD_BACKEND_GARBAGE_COLLECTOR_H_
