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

#ifndef YT_ENUM_UTILS_H
#define YT_ENUM_UTILS_H

#include <exception>
#include <string>

namespace youtils
{

template<typename T>
class EnumParseError
    : public std::exception
{
public:
    EnumParseError(const std::string& str) throw()
        : str_(str)
        , message_(str)
    {
        message_ += " is not one of ";
        for(unsigned i = 0; i < T::size; ++i)
        {
            message_ +=  T::strings[i];
            message_ += " ";
        }
    }

    ~EnumParseError() throw()
    {}

    virtual const char* what() const throw()
    {
        return message_.c_str();
    }
private:
    std::string str_;
    std::string message_;
};

template<typename Enum>
struct EnumTraits;

struct EnumUtils
{
    template<typename Enum>
    static Enum
    fromInteger(unsigned i)
    {
        return static_cast<Enum>(i);
    }

    template<typename Enum,
             typename Traits = EnumTraits<Enum>>
    static std::istream&
    read_from_stream(std::istream& is,
                     Enum& jt)
    {
        std::string val;
        is >> val;
        for(unsigned i = 0; i < sizeof(Traits::strings); ++i)
        {
            if(val == Traits::strings[i])
            {
                jt = fromInteger<Enum>(i);
                return is;
            }
        }
        throw EnumParseError<Traits>(val);
    }

    template<typename Enum,
             typename Traits = EnumTraits<Enum>>
    static std::ostream&
    write_to_stream(std::ostream& os,
                    const Enum& enum_)
    {
        for(unsigned i = 0; i < sizeof(Traits::strings); ++i)
        {
            if(fromInteger<Enum>(i) == enum_)
            {
                std::string s(Traits::strings[i]);
                return (os << s);
            }
        }
        throw "something";
    }
};

}

#endif // !YT_ENUM_UTILS_H
