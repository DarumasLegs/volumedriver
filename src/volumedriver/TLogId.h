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

#ifndef VD_TLOG_ID_H_
#define VD_TLOG_ID_H_

#include <iosfwd>
#include <vector>

#include <boost/serialization/strong_typedef.hpp>

#include <youtils/UUID.h>

// We're not using youtils/OurStrongTypedef.h as we want our own streaming.
namespace volumedriver
{

BOOST_STRONG_TYPEDEF(youtils::UUID,
                     TLogId);

template<class Archive>
void serialize(Archive& ar,
               TLogId& tlog_id,
               const unsigned)
{
    ar & static_cast<youtils::UUID&>(tlog_id);
}

std::ostream&
operator<<(std::ostream&,
           const TLogId&);

std::istream&
operator>>(std::istream&,
           TLogId&);

using OrderedTLogIds = std::vector<TLogId>;

}

#endif // !VD_TLOG_ID_H_
