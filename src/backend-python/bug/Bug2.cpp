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

#include <boost/python/class.hpp>
#include <boost/python/def.hpp>
#include <boost/python/enum.hpp>
#include <boost/python/module.hpp>
#include "Bug2.h"

BOOST_PYTHON_MODULE(Bug2)
{
    using namespace boost::python;

    class_<Bug2,
           boost::noncopyable>("Bug2",
                               init<>
                               ("Creates a Bug1"))
                               .def("call", &Bug2::call)
        .def("call", &Bug2::call)
        .def("__str__",  &Bug2::str)
        .def("__repr__", &Bug2::str)
        ;
}

// Local Variables: **
// End: **
