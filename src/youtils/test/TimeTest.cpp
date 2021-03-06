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

#include "../TestBase.h"
#include "../Time.h"

namespace youtilstest
{
//using namespace youtils;
class TimeTest : public TestBase
{};


TEST_F(TimeTest, test1)
{
    std::string s(Time::getCurrentTimeAsString());

    boost::posix_time::ptime p(Time::timeFromString(s));
    (void)p;
}


}
// Local Variables: **
// mode: c++ **
// End: **
