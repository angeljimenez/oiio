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

#include "dassert.h"
#include "ustring.h"

#include "paramlist.h"


// FIXME: We should clearly be using a namespace.  But maybe not "Gelato".
// using namespace Gelato;


void
ParamValue::init_noclear (ustring _name, TypeDesc _type,
                          int _nvalues, const void *_value, bool _copy)
{
    m_name = _name;
    m_type = _type;
    m_nvalues = _nvalues;
    m_interp = INTERP_CONSTANT;
    size_t n = (size_t) (m_nvalues * m_type.numelements());
    size_t size = (size_t) (m_nvalues * m_type.size());
    bool small = (size <= sizeof(m_data));

    if (_copy || small) {
        if (small) {
            if (_value)
                memcpy (&m_data, _value, size);
            else
                m_data.localval = 0;
            m_copy = false;
            m_nonlocal = false;
        } else {
            m_data.ptr = malloc (size);
            if (_value)
                memcpy ((char *)m_data.ptr, _value, size);
            else
                memset ((char *)m_data.ptr, 0, size);
            m_copy = true;
            m_nonlocal = true;
        }
        if (m_type.basetype == TypeDesc::STRING) {
            ustring *u = (ustring *) data();
            for (size_t i = 0;  i < n;  ++i)
                u[i] = ustring(u[i].c_str());
        }
    } else {
        // Big enough to warrant a malloc, but the caller said don't
        // make a copy
        m_data.ptr = _value;
        m_copy = false;
        m_nonlocal = true;
    }
}



void
ParamValue::clear_value ()
{
    if (m_copy && m_nonlocal && m_data.ptr)
        free ((void *)m_data.ptr);
    m_data.ptr = NULL;
    m_copy = false;
    m_nonlocal = false;
}
