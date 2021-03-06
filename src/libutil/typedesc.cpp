/*
  Copyright 2008 Larry Gritz and the other authors and contributors.
  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the software's owners nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  (This is the Modified BSD License)
*/


#include <cstdio>
#include <cstdlib>
#include <string>

#include "dassert.h"
#include "ustring.h"
#include "strutil.h"

#include "typedesc.h"


// FIXME: We should clearly be using a namespace. 
// using namespace blah;


TypeDesc::TypeDesc (const char *typestring)
{
    ASSERT (0);
}



static int basetype_size[] = {
    0, // UNKNOWN
    0, // VOID
    sizeof(unsigned char),   // UCHAR
    sizeof(char),            // CHAR
    sizeof(unsigned short),  // USHORT
    sizeof(short),           // SHORT
    sizeof(unsigned int),    // UINT
    sizeof(int),             // INT
    sizeof(unsigned long long), // ULONGLONG
    sizeof(long long),       // LONGLONG
    sizeof(float)/2,         // HALF
    sizeof(float),           // FLOAT
    sizeof(double),          // DOUBLE
    sizeof(char *),          // STRING
    sizeof(void *)           // PTR
};


size_t
TypeDesc::basesize () const
{
    DASSERT (sizeof(basetype_size)/sizeof(basetype_size[0]) == TypeDesc::LASTBASE);
    DASSERT (basetype < TypeDesc::LASTBASE);
    return basetype_size[basetype];
}



static const char * basetype_name[] = {
    "unknown",         // UNKNOWN
    "void",            // VOID/NONE
    "uint8",           // UCHAR
    "int8",            // CHAR
    "uint16",          // USHORT
    "int16",           // SHORT
    "uint",            // UINT
    "int",             // INT
    "uint64",          // ULONGLONG
    "int64",           // LONGLONG
    "half",            // HALF
    "float",           // FLOAT
    "double",          // DOUBLE
    "string",          // STRING
    "pointer"          // PTR
};

static const char * basetype_code[] = {
    "unknown",      // UNKNOWN
    "void",         // VOID/NONE
    "uc",           // UCHAR
    "c",            // CHAR
    "us",           // USHORT
    "s",            // SHORT
    "ui",           // UINT
    "i",            // INT
    "ull",          // ULONGLONG
    "ll",           // LONGLONG
    "h",            // HALF
    "f",            // FLOAT
    "d",            // DOUBLE
    "str",          // STRING
    "ptr"           // PTR
};



const char *
TypeDesc::c_str () const
{
    // FIXME : how about a per-thread cache of the last one or two, so
    // we don't have to re-assemble strings all the time?
    std::string result;
    if (aggregate == SCALAR)
        result = basetype_name[basetype];
    else if (aggregate == MATRIX44 && basetype == FLOAT)
        result = "matrix";
    else if (vecsemantics == NOXFORM) {
        const char *agg = "";
        switch (aggregate) {
        case VEC2 : agg = "vec2"; break;
        case VEC3 : agg = "vec3"; break;
        case VEC4 : agg = "vec4"; break;
        case MATRIX44 : agg = "matrix44"; break;
        }
        result = std::string (agg) + basetype_code[basetype];
    } else {
        // Special names for vector semantics
        const char *vec = "";
        switch (vecsemantics) {
        case COLOR  : vec = "color"; break;
        case POINT  : vec = "point"; break;
        case VECTOR : vec = "vector"; break;
        case NORMAL : vec = "normal"; break;
        default: ASSERT (0 && "Invalid vector semantics");
        }
        const char *agg = "";
        switch (aggregate) {
        case VEC2 : agg = "2"; break;
        case VEC4 : agg = "4"; break;
        case MATRIX44 : agg = "matrix"; break;
        }
        result = std::string (vec) + std::string (agg);
        if (basetype != FLOAT)
            result += basetype_code[basetype];
    }
    if (arraylen > 0)
        result += Strutil::format ("[%d]", arraylen);
    else if (arraylen < 0)
        result += "[]";
    return ustring(result).c_str();
}



int
TypeDesc::fromstring (const char *typestring, char *shortname)
{
    // FIXME
    ASSERT(0);
    return 0;
}



const TypeDesc TypeDesc::TypeFloat (TypeDesc::FLOAT);
const TypeDesc TypeDesc::TypeColor (TypeDesc::FLOAT, TypeDesc::VEC3, TypeDesc::COLOR);
const TypeDesc TypeDesc::TypePoint (TypeDesc::FLOAT, TypeDesc::VEC3, TypeDesc::POINT);
const TypeDesc TypeDesc::TypeVector (TypeDesc::FLOAT, TypeDesc::VEC3, TypeDesc::VECTOR);
const TypeDesc TypeDesc::TypeNormal (TypeDesc::FLOAT, TypeDesc::VEC3, TypeDesc::NORMAL);
const TypeDesc TypeDesc::TypeMatrix (TypeDesc::FLOAT,TypeDesc::MATRIX44);
const TypeDesc TypeDesc::TypeString (TypeDesc::STRING);
const TypeDesc TypeDesc::TypeInt (TypeDesc::INT);

