/*
  Copyright 2010 Larry Gritz and the other authors and contributors.
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

#include <fstream>

#include "imageio.h"

using namespace OpenImageIO;



class PNMOutput : public ImageOutput {
public:
    virtual ~PNMOutput ();
    virtual const char * format_name (void) const { return "pnm"; }
    virtual bool supports (const std::string &feature) const {
        // Support nothing nonstandard
        return false;
    }
    virtual bool open (const std::string &name, const ImageSpec &spec,
                       bool append=false);
    virtual bool close ();
    virtual bool write_scanline (int y, int z, TypeDesc format,
                                 const void *data, stride_t xstride);

private:
    std::string m_filename;           ///< Stash the filename
    std::ofstream m_file;
    unsigned int m_max_val, m_pnm_type;
};



// Obligatory material to make this a recognizeable imageio plugin:
extern "C" {

    DLLEXPORT ImageOutput *pnm_output_imageio_create () { return new PNMOutput; }

    DLLEXPORT const char * pnm_output_extensions[] = {
        "ppm","pgm","pbm","pnm", NULL
    };

};




inline void
write_ascii_binary (std::ostream & file, const unsigned char * data, 
                    const stride_t stride, const ImageSpec & spec)
{
    for (int x = 0; x < spec.width; x++) 
        file << (data[x * stride] ? 0 : 1) << "\n"; 
}



inline void
write_raw_binary (std::ostream & file, const unsigned char * data, 
                  const stride_t stride, const ImageSpec & spec)
{
    unsigned char val;
    for (int x = 0; x < spec.width;) {
        val=0;
        for (int bit=7; bit >= 0 && x < spec.width; x++, bit--) 
            val += (data[x * stride] ? 0 : (1 << bit));
        file.write ((char*)&val, sizeof (char));
    }
}



template <class T>
inline void
write_ascii (std::ostream &file, const T *data, const stride_t stride, 
             const ImageSpec &spec, unsigned int max_val)
{
    unsigned int pixel, val;
    for (int x = 0; x < spec.width; x++){
        pixel = x * stride;
        for (int c = 0; c < spec.nchannels; c++) {
            val = data [pixel + c];
            val = val * max_val / std::numeric_limits<T>::max();
            file << val << "\n"; 
        }
    }
}



template <class T>
inline void
write_raw (std::ostream &file, const T *data, const stride_t stride, 
           const ImageSpec &spec, unsigned int max_val)
{
    unsigned int pixel, val;
    for (int x = 0; x < spec.width; x++) {
        pixel = x * stride;
        for (int c = 0; c < spec.nchannels; c++) {
            val = data[pixel + c];
            val = val * max_val / std::numeric_limits<T>::max();
            file.write ((char*)&val, sizeof (T));
        }
    }
}



PNMOutput::~PNMOutput ()
{
    close ();
}



bool
PNMOutput::open (const std::string &name, const ImageSpec &userspec, bool append)
{
    if (append)
        return false;
    m_spec=userspec;

    m_file.open (name.c_str());
    if (!m_file.is_open())
        return false;

    int bits_per_sample = m_spec.get_int_attribute ("BitsPerSample", 8);

    if (bits_per_sample == 1)
        m_pnm_type = 4;
    else if (m_spec.nchannels == 1)
        m_pnm_type = 5;
    else 
        m_pnm_type = 6;
    if (!m_spec.get_int_attribute ("pnm:binary", 1)) 
        m_pnm_type -= 3;

    m_max_val = (1 << bits_per_sample) - 1;
    // Write header
    m_file << "P" << m_pnm_type << std::endl;
    m_file << m_spec.width << " " << m_spec.height << std::endl;
    if (m_pnm_type != 1 && m_pnm_type != 4)  // only non-monochrome 
        m_file << m_max_val << std::endl;

    return m_file.good();
}



bool
PNMOutput::close ()
{
    m_file.close();
    return true;  
}



bool
PNMOutput::write_scanline (int y, int z, TypeDesc format,
        const void *data, stride_t xstride)
{
    if (!m_file.is_open())
        return false;
    if (z)
        return false;
    switch (m_pnm_type){
        case 1:
            write_ascii_binary (m_file, (unsigned char *) data, xstride, m_spec);
            break;
        case 2:
        case 3:
            if (m_max_val > std::numeric_limits<unsigned char>::max())
                write_ascii (m_file, (unsigned short *) data, xstride, m_spec, m_max_val);
            else 
                write_ascii (m_file, (unsigned char *) data, xstride, m_spec, m_max_val);
            break;
        case 4:
            write_raw_binary (m_file, (unsigned char *) data, xstride, m_spec);
            break;
        case 5:
        case 6:
            if (m_max_val > std::numeric_limits<unsigned char>::max())
                write_raw (m_file, (unsigned short *) data, xstride, m_spec, m_max_val);
            else 
                write_raw (m_file, (unsigned char *) data, xstride, m_spec, m_max_val);
            break;
        default:
            return false;
    } 

    return m_file.good();
}
