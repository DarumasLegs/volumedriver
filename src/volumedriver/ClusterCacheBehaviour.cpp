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

#include "ClusterCacheBehaviour.h"

#include <iostream>

#include <boost/bimap.hpp>

#include <youtils/StreamUtils.h>

namespace volumedriver
{

namespace yt = youtils;

namespace
{

void
reminder(ClusterCacheBehaviour) __attribute__((unused));

void
reminder(ClusterCacheBehaviour m)
{
    switch (m)
    {
    case ClusterCacheBehaviour::CacheOnWrite:
    case ClusterCacheBehaviour::CacheOnRead:
    case ClusterCacheBehaviour::NoCache:
        // If the compiler yells at you that you've forgotten dealing with an enum
        // value here chances are that it's also missing from the translations map
        // below. If so add it NOW.
        break;
    }
}

using TranslationsMap = boost::bimap<ClusterCacheBehaviour, std::string>;

TranslationsMap
init_translations()
{
    const std::vector<TranslationsMap::value_type> initv{
        { ClusterCacheBehaviour::CacheOnWrite, "CacheOnWrite" },
        { ClusterCacheBehaviour::CacheOnRead, "CacheOnRead" },
        { ClusterCacheBehaviour::NoCache, "NoCache" },
    };

    return TranslationsMap(initv.begin(),
                           initv.end());
}

}

std::ostream&
operator<<(std::ostream& os,
           const ClusterCacheBehaviour b)
{
    static const TranslationsMap translations(init_translations());
    return yt::StreamUtils::stream_out(translations.left,
                                       os,
                                       b);
}

std::istream&
operator>>(std::istream& is,
           ClusterCacheBehaviour& b)
{
    static const TranslationsMap translations(init_translations());
    return yt::StreamUtils::stream_in(translations.right,
                                      is,
                                      b);
}

}
