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
#include <cmath>
#include <iostream>

#include <tiffio.h>

#include <boost/algorithm/string.hpp>
using boost::algorithm::iequals;

#include "dassert.h"
#include "imageio.h"
#include "strutil.h"
#include "sysutil.h"


using namespace OpenImageIO;


class TIFFOutput : public ImageOutput {
public:
    TIFFOutput ();
    virtual ~TIFFOutput ();
    virtual const char * format_name (void) const { return "tiff"; }
    virtual bool supports (const std::string &feature) const;
    virtual bool open (const std::string &name, const ImageSpec &spec,
                       bool append=false);
    virtual bool close ();
    virtual bool write_scanline (int y, int z, TypeDesc format,
                                 const void *data, stride_t xstride);
    virtual bool write_tile (int x, int y, int z,
                             TypeDesc format, const void *data,
                             stride_t xstride, stride_t ystride, stride_t zstride);

private:
    TIFF *m_tif;
    std::vector<unsigned char> m_scratch;
    int m_planarconfig;

    // Initialize private members to pre-opened state
    void init (void) {
        m_tif = NULL;
    }

    // Convert planar contiguous to planar separate data format
    void contig_to_separate (int n, const unsigned char *contig,
                             unsigned char *separate);
    // Add a parameter to the output
    bool put_parameter (const std::string &name, TypeDesc type,
                        const void *data);
};




// Obligatory material to make this a recognizeable imageio plugin:
extern "C" {

DLLEXPORT ImageOutput *tiff_output_imageio_create () { return new TIFFOutput; }

DLLEXPORT int tiff_imageio_version = OPENIMAGEIO_PLUGIN_VERSION;

DLLEXPORT const char * tiff_output_extensions[] = {
    "tiff", "tif", "tx", "env", "sm", "vsm", NULL
};

};



TIFFOutput::TIFFOutput ()
{
    init ();
}



TIFFOutput::~TIFFOutput ()
{
    // Close, if not already done.
    close ();
}



bool
TIFFOutput::supports (const std::string &feature) const
{
    if (feature == "tiles")
        return true;
    if (feature == "multiimage")
        return true;

    // FIXME: we could support "volumes" and "empty"

    // Everything else, we either don't support or don't know about
    return false;
}



bool
TIFFOutput::open (const std::string &name, const ImageSpec &userspec, bool append)
{
    close ();  // Close any already-opened file
    m_spec = userspec;  // Stash the spec

    // Check for things this format doesn't support
    if (m_spec.width < 1 || m_spec.height < 1) {
        error ("Image resolution must be at least 1x1, you asked for %d x %d",
               m_spec.width, m_spec.height);
        return false;
    }
    if (m_spec.depth < 1)
        m_spec.depth = 1;

    // Open the file
    m_tif = TIFFOpen (name.c_str(), append ? "a" : "w");
    if (! m_tif) {
        error ("Can't open \"%s\" for output.", name.c_str());
        return false;
    }

    TIFFSetField (m_tif, TIFFTAG_XPOSITION, (float)m_spec.x);
    TIFFSetField (m_tif, TIFFTAG_YPOSITION, (float)m_spec.y);
    TIFFSetField (m_tif, TIFFTAG_IMAGEWIDTH, m_spec.width);
    TIFFSetField (m_tif, TIFFTAG_IMAGELENGTH, m_spec.height);
    if ((m_spec.full_width != 0 || m_spec.full_height != 0) &&
        (m_spec.full_width != m_spec.width || m_spec.full_height != m_spec.height)) {
        TIFFSetField (m_tif, TIFFTAG_PIXAR_IMAGEFULLWIDTH, m_spec.full_width);
        TIFFSetField (m_tif, TIFFTAG_PIXAR_IMAGEFULLLENGTH, m_spec.full_height);
    }
    if (m_spec.tile_width) {
        TIFFSetField (m_tif, TIFFTAG_TILEWIDTH, m_spec.tile_width);
        TIFFSetField (m_tif, TIFFTAG_TILELENGTH, m_spec.tile_height);
    } else {
        // Scanline images must set rowsperstrip
        TIFFSetField (m_tif, TIFFTAG_ROWSPERSTRIP, 32);
    }
    TIFFSetField (m_tif, TIFFTAG_SAMPLESPERPIXEL, m_spec.nchannels);
    TIFFSetField (m_tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT); // always
    
    int bps, sampformat;
    switch (m_spec.format.basetype) {
    case TypeDesc::INT8:
        bps = 8;
        sampformat = SAMPLEFORMAT_INT;
        break;
    case TypeDesc::UINT8:
        bps = 8;
        sampformat = SAMPLEFORMAT_UINT;
        break;
    case TypeDesc::INT16:
        bps = 16;
        sampformat = SAMPLEFORMAT_INT;
        break;
    case TypeDesc::UINT16:
        bps = 16;
        sampformat = SAMPLEFORMAT_UINT;
        break;
    case TypeDesc::HALF:
        // Silently change requests for unsupported 'half' to 'float'
        m_spec.set_format (TypeDesc::FLOAT);
    case TypeDesc::FLOAT:
        bps = 32;
        sampformat = SAMPLEFORMAT_IEEEFP;
        break;
    case TypeDesc::DOUBLE:
        bps = 64;
        sampformat = SAMPLEFORMAT_IEEEFP;
        break;
    default:
        error ("TIFF doesn't support %s images (\"%s\")",
               m_spec.format.c_str(), name.c_str());
        close();
        return false;
    }
    TIFFSetField (m_tif, TIFFTAG_BITSPERSAMPLE, bps);
    TIFFSetField (m_tif, TIFFTAG_SAMPLEFORMAT, sampformat);

    int photo = (m_spec.nchannels > 1 ? PHOTOMETRIC_RGB : PHOTOMETRIC_MINISBLACK);
    TIFFSetField (m_tif, TIFFTAG_PHOTOMETRIC, photo);

    if (m_spec.nchannels == 4 && m_spec.alpha_channel == m_spec.nchannels-1) {
        unsigned short s = EXTRASAMPLE_ASSOCALPHA;
        TIFFSetField (m_tif, TIFFTAG_EXTRASAMPLES, 1, &s);
    }

    // Default to LZW compression if no request came with the user spec
    if (! m_spec.find_attribute("compression"))
        m_spec.attribute ("compression", "lzw");

    ImageIOParameter *param;
    const char *str = NULL;

    // Did the user request separate planar configuration?
    m_planarconfig = PLANARCONFIG_CONTIG;
    if ((param = m_spec.find_attribute("planarconfig", TypeDesc::STRING)) ||
        (param = m_spec.find_attribute("tiff:planarconfig", TypeDesc::STRING))) {
        str = *(char **)param->data();
        if (str && ! strcmp (str, "separate"))
            m_planarconfig = PLANARCONFIG_SEPARATE;
    }
    TIFFSetField (m_tif, TIFFTAG_PLANARCONFIG, m_planarconfig);

    // Automatically set date field if the client didn't supply it.
    if (! m_spec.find_attribute("DateTime")) {
        time_t now;
        time (&now);
        struct tm mytm;
        Sysutil::get_local_time (&now, &mytm);
        std::string date = Strutil::format ("%4d:%02d:%02d %2d:%02d:%02d",
                               mytm.tm_year+1900, mytm.tm_mon+1, mytm.tm_mday,
                               mytm.tm_hour, mytm.tm_min, mytm.tm_sec);
        m_spec.attribute ("DateTime", date);
    }

    if (m_spec.linearity == ImageSpec::sRGB)
        m_spec.attribute ("Exif:ColorSpace", 1);

    // Deal with all other params
    for (size_t p = 0;  p < m_spec.extra_attribs.size();  ++p)
        put_parameter (m_spec.extra_attribs[p].name().string(),
                       m_spec.extra_attribs[p].type(),
                       m_spec.extra_attribs[p].data());

    std::vector<char> iptc;
    encode_iptc_iim (m_spec, iptc);
    if (iptc.size()) {
        iptc.resize ((iptc.size()+3) & (0xffff-3));  // round up
        TIFFSetField (m_tif, TIFFTAG_RICHTIFFIPTC, iptc.size()/4, &iptc[0]);
    }

    std::string xmp = OpenImageIO::encode_xmp (m_spec, true);
    if (! xmp.empty())
        TIFFSetField (m_tif, TIFFTAG_XMLPACKET, xmp.size(), xmp.c_str());

    TIFFCheckpointDirectory (m_tif);  // Ensure the header is written early

    return true;
}



bool
TIFFOutput::put_parameter (const std::string &name, TypeDesc type,
                           const void *data)
{
    if (iequals(name, "Artist") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_ARTIST, *(char**)data);
        return true;
    }
    if (iequals(name, "Compression") && type == TypeDesc::STRING) {
        int compress = COMPRESSION_LZW;  // default
        const char *str = *(char **)data;
        if (str) {
            if (! strcmp (str, "none"))
                compress = COMPRESSION_NONE;
            else if (! strcmp (str, "lzw"))
                compress = COMPRESSION_LZW;
            else if (! strcmp (str, "zip") || ! strcmp (str, "deflate"))
                compress = COMPRESSION_ADOBE_DEFLATE;
            else if (! strcmp (str, "packbits"))
                compress = COMPRESSION_PACKBITS;
            else if (! strcmp (str, "ccittrle"))
                compress = COMPRESSION_CCITTRLE;
        }
        TIFFSetField (m_tif, TIFFTAG_COMPRESSION, compress);
        // Use predictor when using compression
        if (compress == COMPRESSION_LZW || compress == COMPRESSION_ADOBE_DEFLATE) {
            if (m_spec.format == TypeDesc::FLOAT || m_spec.format == TypeDesc::DOUBLE || m_spec.format == TypeDesc::HALF) {
                // TIFFSetField (m_tif, TIFFTAG_PREDICTOR, PREDICTOR_FLOATINGPOINT);

                // Older versions of libtiff did not support this
                // predictor.  So to prevent us from writing TIFF files
                // that certain apps can't read, don't use it. Ugh.
                // FIXME -- lift this restriction when we think the newer
                // libtiff is widespread enough to no longer worry about this.
            }
            else
                TIFFSetField (m_tif, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
        }
    }
    if (iequals(name, "Copyright") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_COPYRIGHT, *(char**)data);
        return true;
    }
    if (iequals(name, "DateTime") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_DATETIME, *(char**)data);
        return true;
    }
    if ((iequals(name, "name") || iequals(name, "DocumentName")) && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_DOCUMENTNAME, *(char**)data);
        return true;
    }
    if (iequals(name,"fovcot") && type == TypeDesc::FLOAT) {
        double d = *(float *)data;
        TIFFSetField (m_tif, TIFFTAG_PIXAR_FOVCOT, d);
        return true;
    }
    if ((iequals(name, "host") || iequals(name, "HostComputer")) && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_HOSTCOMPUTER, *(char**)data);
        return true;
    }
    if ((iequals(name, "description") || iequals(name, "ImageDescription")) &&
          type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_IMAGEDESCRIPTION, *(char**)data);
        return true;
    }
    if (iequals(name, "tiff:Predictor") && type == TypeDesc::INT) {
        TIFFSetField (m_tif, TIFFTAG_PREDICTOR, *(int *)data);
        return true;
    }
    if (iequals(name, "ResolutionUnit") && type == TypeDesc::STRING) {
        const char *s = *(char**)data;
        bool ok = true;
        if (! strcmp (s, "none"))
            TIFFSetField (m_tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE);
        else if (! strcmp (s, "in") || ! strcmp (s, "inch"))
            TIFFSetField (m_tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
        else if (! strcmp (s, "cm"))
            TIFFSetField (m_tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_CENTIMETER);
        else ok = false;
        return ok;
    }
    if (iequals(name, "ResolutionUnit") && type == TypeDesc::UINT) {
        TIFFSetField (m_tif, TIFFTAG_RESOLUTIONUNIT, *(unsigned int *)data);
        return true;
    }
    if (iequals(name, "tiff:RowsPerStrip")) {
        if (type == TypeDesc::INT) {
            TIFFSetField (m_tif, TIFFTAG_ROWSPERSTRIP, *(int*)data);
            return true;
        } else if (type == TypeDesc::STRING) {
            // Back-compatibility with Entropy and PRMan
            TIFFSetField (m_tif, TIFFTAG_ROWSPERSTRIP, atoi(*(char **)data));
            return true;
        }
    }
    if (iequals(name, "Software") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_SOFTWARE, *(char**)data);
        return true;
    }
    if (iequals(name, "tiff:SubFileType") && type == TypeDesc::INT) {
        TIFFSetField (m_tif, TIFFTAG_SUBFILETYPE, *(int*)data);
        return true;
    }
    if (iequals(name, "textureformat") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_PIXAR_TEXTUREFORMAT, *(char**)data);
        return true;
    }
    if (iequals(name, "wrapmodes") && type == TypeDesc::STRING) {
        TIFFSetField (m_tif, TIFFTAG_PIXAR_WRAPMODES, *(char**)data);
        return true;
    }
    if (iequals(name, "worldtocamera") && type == PT_MATRIX) {
        TIFFSetField (m_tif, TIFFTAG_PIXAR_MATRIX_WORLDTOCAMERA, data);
        return true;
    }
    if (iequals(name, "worldtoscreen") && type == PT_MATRIX) {
        TIFFSetField (m_tif, TIFFTAG_PIXAR_MATRIX_WORLDTOSCREEN, data);
        return true;
    }
    if (iequals(name, "XResolution") && type == TypeDesc::FLOAT) {
        TIFFSetField (m_tif, TIFFTAG_XRESOLUTION, *(float *)data);
        return true;
    }
    if (iequals(name, "YResolution") && type == TypeDesc::FLOAT) {
        TIFFSetField (m_tif, TIFFTAG_YRESOLUTION, *(float *)data);
        return true;
    }

    // FIXME -- we don't currently support writing of EXIF fields.  TIFF
    // in theory allows it, using a custom IFD directory, but at
    // present, it appears that libtiff only supports reading custom
    // IFD's, not writing them.

    return false;
}



bool
TIFFOutput::close ()
{
    if (m_tif)
        TIFFClose (m_tif);
    init ();      // re-initialize
    return true;  // How can we fail?
}



/// Helper: Convert n pixels from contiguous (RGBRGBRGB) to separate
/// (RRRGGGBBB) planarconfig.
void
TIFFOutput::contig_to_separate (int n, const unsigned char *contig,
                                unsigned char *separate)
{
    int channelbytes = m_spec.channel_bytes();
    for (int p = 0;  p < n;  ++p)                     // loop over pixels
        for (int c = 0;  c < m_spec.nchannels;  ++c)    // loop over channels
            for (int i = 0;  i < channelbytes;  ++i)  // loop over data bytes
                separate[(c*n+p)*channelbytes+i] =
                    contig[(p*m_spec.nchannels+c)*channelbytes+i];
}



bool
TIFFOutput::write_scanline (int y, int z, TypeDesc format,
                            const void *data, stride_t xstride)
{
    m_spec.auto_stride (xstride, format, spec().nchannels);
    const void *origdata = data;
    data = to_native_scanline (format, data, xstride, m_scratch);

    y -= m_spec.y;
    if (m_planarconfig == PLANARCONFIG_SEPARATE && m_spec.nchannels > 1) {
        // Convert from contiguous (RGBRGBRGB) to separate (RRRGGGBBB)
        m_scratch.resize (m_spec.scanline_bytes());
        contig_to_separate (m_spec.width, (const unsigned char *)data, &m_scratch[0]);
        TIFFWriteScanline (m_tif, &m_scratch[0], y);
    } else {
        // No contig->separate is necessary.  But we still use scratch
        // space since TIFFWriteScanline is destructive when
        // TIFFTAG_PREDICTOR is used.
        if (data == origdata) {
            m_scratch.assign ((unsigned char *)data,
                              (unsigned char *)data+m_spec.scanline_bytes());
            data = &m_scratch[0];
        }
        TIFFWriteScanline (m_tif, (tdata_t)data, y);
    }
    // Every 16 scanlines, checkpoint (write partial file)
    if ((y % 16) == 0)
        TIFFCheckpointDirectory (m_tif);

    return true;
}



bool
TIFFOutput::write_tile (int x, int y, int z,
                        TypeDesc format, const void *data,
                        stride_t xstride, stride_t ystride, stride_t zstride)
{
    m_spec.auto_stride (xstride, ystride, zstride, format, spec().nchannels,
                        spec().tile_width, spec().tile_height);
    x -= m_spec.x;   // Account for offset, so x,y are file relative, not 
    y -= m_spec.y;   // image relative
    const void *origdata = data;   // Stash original pointer
    data = to_native_tile (format, data, xstride, ystride, zstride, m_scratch);
    if (m_planarconfig == PLANARCONFIG_SEPARATE && m_spec.nchannels > 1) {
        // Convert from contiguous (RGBRGBRGB) to separate (RRRGGGBBB)
        int tile_pixels = m_spec.tile_width * m_spec.tile_height 
                            * std::max (m_spec.tile_depth, 1);
        int plane_bytes = tile_pixels * m_spec.format.size();
        DASSERT (imagesize_t(plane_bytes*m_spec.nchannels) == m_spec.tile_bytes());
        m_scratch.resize (m_spec.tile_bytes());
        contig_to_separate (tile_pixels, (const unsigned char *)data, &m_scratch[0]);
        for (int c = 0;  c < m_spec.nchannels;  ++c)
            TIFFWriteTile (m_tif, (tdata_t)&m_scratch[plane_bytes*c], x, y, z, c);
    } else {
        // No contig->separate is necessary.  But we still use scratch
        // space since TIFFWriteTile is destructive when
        // TIFFTAG_PREDICTOR is used.
        if (data == origdata) {
            m_scratch.assign ((unsigned char *)data,
                              (unsigned char *)data + m_spec.tile_bytes());
            data = &m_scratch[0];
        }
        TIFFWriteTile (m_tif, (tdata_t)data, x, y, z, 0);
    }

    // Every row of tiles, checkpoint (write partial file)
    if ((y % m_spec.tile_height) == 0)
        TIFFCheckpointDirectory (m_tif);

    return true;
}
