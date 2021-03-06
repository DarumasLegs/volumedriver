// This file is dual licensed GPLv2 and Apache 2.0.
// Active license depends on how it is used.
//
// Copyright 2016 iNuron NV
//
// // GPL //
// This file is part of OpenvStorage.
//
// OpenvStorage is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenvStorage. If not, see <http://www.gnu.org/licenses/>.
//
// // Apache 2.0 //
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PYTHON_LOGGING_H_
#define PYTHON_LOGGING_H_

#include <boost/python.hpp>
#include "Logger.h"
#include "IOException.h"

#include <string>
#include <boost/filesystem/path.hpp>

namespace pythonyoutils
{
using youtils::Logger;
using youtils::Severity;


class Logging
{
    MAKE_EXCEPTION(LoggingException, fungi::IOException);
    MAKE_EXCEPTION(LoggingAlreadyConfiguredException, LoggingException)
public:
    Logging() = delete;

    Logging&
    operator=(const Logging&) = delete;

    static void
    disableLogging()
    {
        Logger::disableLogging();
    }

    static void
    enableLogging()
    {
        Logger::enableLogging();
    }

    static bool
    loggingEnabled()
    {
        return Logger::loggingEnabled();
    }

    static void
    setupConsoleLogging(Severity severity = Severity::trace,
                        const std::string& progname = "PythonLogger")
    {
        if(loggin_is_setup_)
        {
            throw LoggingAlreadyConfiguredException("Logging already set up");
        }
        else
        {
            const std::vector<std::string> sinks = { Logger::console_sink_name() };
            Logger::setupLogging(progname,
                                 sinks,
                                 severity,
                                 youtils::LogRotation::F);
            loggin_is_setup_ = true;

        }
    }

    static void
    setupFileLogging(const std::string& path,
                     const Severity severity = Severity::trace,
                     const std::string& progname = "PythonLogger")
    {
        if(loggin_is_setup_)
        {
            throw LoggingAlreadyConfiguredException("Logging already set up");
        }
        else
        {
            const std::vector<std::string> sinks = { path };

            Logger::setupLogging(progname,
                                 sinks,
                                 severity,
                                 youtils::LogRotation::F);
            loggin_is_setup_ = true;
        }
    }

    std::string
    str() const
    {
        using namespace std::string_literals;
        return "<Logging>"s;
    }


    std::string
    repr() const
    {
        using namespace std::string_literals;
        return "<Logging>"s;
    }

    static bool loggin_is_setup_ ;

};

}

#endif // PYTHON_LOGGING_H_
