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
#include <ctime>
#include <iostream>
#include <iterator>

#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathVec.h>

#include "argparse.h"
#include "imageio.h"
using namespace OpenImageIO;
#include "ustring.h"
#include "imagebuf.h"
#include "texture.h"
#include "fmath.h"
#include "sysutil.h"
#include "strutil.h"


static std::vector<std::string> filenames;
static std::string output_filename = "out.exr";
static bool verbose = false;
static int output_xres = 512, output_yres = 512;
static float blur = 0;
static float width = 1;
static int iters = 1;
static int autotile = 0;
static bool automip = false;
static TextureSystem *texsys = NULL;
static std::string searchpath;
static int blocksize = 1;
static bool nowarp = false;
static float cachesize = -1;
static int maxfiles = -1;
static float missing[4] = {-1, 0, 0, 1};



static int
parse_files (int argc, const char *argv[])
{
    for (int i = 0;  i < argc;  i++)
        filenames.push_back (argv[i]);
    return 0;
}



static void
getargs (int argc, const char *argv[])
{
    bool help = false;
    ArgParse ap;
    ap.options ("Usage:  testtex [options] inputfile",
                  "%*", parse_files, "",
                  "--help", &help, "Print help message",
                  "-v", &verbose, "Verbose status messages",
                  "-o %s", &output_filename, "Output test image",
                  "-res %d %d", &output_xres, &output_yres,
                      "Resolution of output test image",
                  "-iters %d", &iters,
                      "Iterations for time trials",
                  "--blur %f", &blur, "Add blur to texture lookup",
                  "--width %f", &width, "Multiply filter width of texture lookup",
                  "--missing %f %f %f", &missing[0], &missing[1], &missing[2],
                        "Specify missing texture color",
                  "--autotile %d", &autotile, "Set auto-tile size for the image cache",
                  "--automip", &automip, "Set auto-MIPmap for the image cache",
                  "--blocksize %d", &blocksize, "Set blocksize (n x n) for batches",
                  "--searchpath %s", &searchpath, "Search path for files",
                  "--nowarp", &nowarp, "Do not warp the image->texture mapping",
                  "--cachesize %g", &cachesize, "Set cache size, in MB",
                  "--maxfiles %d", &maxfiles, "Set maximum open files",
                  NULL);
    if (ap.parse (argc, argv) < 0) {
        std::cerr << ap.geterror() << std::endl;
        ap.usage ();
        exit (EXIT_FAILURE);
    }
    if (help) {
        ap.usage ();
        exit (EXIT_FAILURE);
    }

    if (filenames.size() < 1) {
        std::cerr << "testtex: Must have at least one input file\n";
        ap.usage();
        exit (EXIT_FAILURE);
    }
}



static void
test_gettextureinfo (ustring filename)
{
    bool ok;

    int res[2] = {0};
    ok = texsys->get_texture_info (filename, ustring("resolution"),
                                   TypeDesc(TypeDesc::INT,2), res);
    std::cerr << "Result of get_texture_info resolution = " << ok << ' ' << res[0] << 'x' << res[1] << "\n";

    int chan = 0;
    ok = texsys->get_texture_info (filename, ustring("channels"),
                                   TypeDesc::INT, &chan);
    std::cerr << "Result of get_texture_info channels = " << ok << ' ' << chan << "\n";

    float fchan = 0;
    ok = texsys->get_texture_info (filename, ustring("channels"),
                                   TypeDesc::FLOAT, &fchan);
    std::cerr << "Result of get_texture_info channels = " << ok << ' ' << fchan << "\n";

    int dataformat = 0;
    ok = texsys->get_texture_info (filename, ustring("format"),
                                   TypeDesc::INT, &dataformat);
    std::cerr << "Result of get_texture_info data format = " << ok << ' ' 
              << TypeDesc((TypeDesc::BASETYPE)dataformat).c_str() << "\n";

    const char *datetime = NULL;
    ok = texsys->get_texture_info (filename, ustring("DateTime"),
                                   TypeDesc::STRING, &datetime);
    std::cerr << "Result of get_texture_info datetime = " << ok << ' ' 
              << (datetime ? datetime : "") << "\n";

    const char *texturetype = NULL;
    ok = texsys->get_texture_info (filename, ustring("textureformat"),
                                   TypeDesc::STRING, &texturetype);
    std::cerr << "Texture type is " << ok << ' '
              << (texturetype ? texturetype : "") << "\n";
    std::cerr << "\n";
}


inline Imath::V3f
warp (float x, float y, Imath::M33f &xform)
{
    Imath::V3f coord (x, y, 1.0f);
    coord *= xform;
    coord[0] *= 1/(1+2*std::max (-0.5f, coord[1]));
    return coord;
}


static void
test_plain_texture ()
{
    std::cerr << "Testing 2d texture " << filenames[0] << ", output = " 
              << output_filename << "\n";
    const int nchannels = 4;
    ImageSpec outspec (output_xres, output_yres, nchannels, TypeDesc::HALF);
    ImageBuf image (output_filename, outspec);
    image.zero ();

    Imath::M33f scale;  scale.scale (Imath::V2f (0.5, 0.5));
    Imath::M33f rot;    rot.rotate (radians(30.0f));
    Imath::M33f trans;  trans.translate (Imath::V2f (0.35f, 0.15f));
    Imath::M33f xform = scale * rot * trans;
    xform.invert();

    TextureOptions opt;
    opt.sblur = blur;
    opt.tblur = blur;
    opt.swidth = width;
    opt.twidth = width;
    opt.nchannels = nchannels;
    float fill = 1;
    opt.fill = fill;
    if (missing[0] >= 0)
        opt.missingcolor.init ((float *)&missing, 0);

//    opt.interpmode = TextureOptions::InterpSmartBicubic;
//    opt.mipmode = TextureOptions::MipModeAniso;
    opt.swrap = opt.twrap = TextureOptions::WrapPeriodic;
//    opt.twrap = TextureOptions::WrapBlack;
    int shadepoints = blocksize*blocksize;
    float *s = ALLOCA (float, shadepoints);
    float *t = ALLOCA (float, shadepoints);
    Runflag *runflags = ALLOCA (Runflag, shadepoints);
    float *dsdx = ALLOCA (float, shadepoints);
    float *dtdx = ALLOCA (float, shadepoints);
    float *dsdy = ALLOCA (float, shadepoints);
    float *dtdy = ALLOCA (float, shadepoints);
    float *result = ALLOCA (float, shadepoints*nchannels);
    
    ustring filename = ustring (filenames[0]);

    for (int iter = 0;  iter < iters;  ++iter) {
        if (iters > 1 && filenames.size() > 1) {
            // Use a different filename for each iteration
            int texid = std::min (iter, (int)filenames.size()-1);
            filename = ustring (filenames[texid]);
            std::cerr << "iter " << iter << " file " << filename << "\n";
        }

        // Iterate over blocks
        for (int by = 0, b = 0;  by < output_yres;  by+=blocksize) {
            for (int bx = 0;  bx < output_xres;  bx+=blocksize, ++b) {
                // Trick: switch to other textures on later iterations, if any
                if (iters == 1 && filenames.size() > 1) {
                    // Use a different filename from block to block
                    filename = ustring (filenames[b % (int)filenames.size()]);
                }

                // Process pixels within a block.  First save the texture warp
                // (s,t) and derivatives into SIMD vectors.
                int idx = 0;
                for (int y = by; y < by+blocksize; ++y) {
                    for (int x = bx; x < bx+blocksize; ++x) {
                        if (x < output_xres && y < output_yres) {
                            if (nowarp) {
                                s[idx] = (float)x/output_xres;
                                t[idx] = (float)y/output_yres;
                                dsdx[idx] = 1.0f/output_xres;
                                dtdx[idx] = 0;
                                dsdy[idx] = 0;
                                dtdy[idx] = 1.0f/output_yres;
                            } else {
                                Imath::V3f coord = warp ((float)x/output_xres,
                                                         (float)y/output_yres,
                                                         xform);
                                Imath::V3f coordx = warp ((float)(x+1)/output_xres,
                                                          (float)y/output_yres,
                                                          xform);
                                Imath::V3f coordy = warp ((float)x/output_xres,
                                                          (float)(y+1)/output_yres,
                                                          xform);
                                s[idx] = coord[0];
                                t[idx] = coord[1];
                                dsdx[idx] = coordx[0] - coord[0];
                                dtdx[idx] = coordx[1] - coord[1];
                                dsdy[idx] = coordy[0] - coord[0];
                                dtdy[idx] = coordy[1] - coord[1];
                            }
                            runflags[idx] = RunFlagOn;
                        } else {
                            runflags[idx] = RunFlagOff;
                        }
                        ++idx;
                    }
                }
                // Call the texture system to do the filtering.
                bool ok = texsys->texture (filename, opt, runflags, 0, shadepoints,
                                           Varying(s), Varying(t),
                                           Varying(dsdx), Varying(dtdx),
                                           Varying(dsdy), Varying(dtdy), result);
                if (! ok) {
                    std::string e = texsys->geterror ();
                    if (! e.empty())
                        std::cerr << "ERROR: " << e << "\n";
                }

                // Save filtered pixels back to the image.
                idx = 0;
                for (int y = by; y < by+blocksize; ++y) {
                    for (int x = bx; x < bx+blocksize; ++x) {
                        if (runflags[idx]) {
                            image.setpixel (x, y, result + idx*nchannels);
                        }
                        ++idx;
                    }
                }
            }
        }
    }
    
    if (! image.save ()) 
        std::cerr << "Error writing " << output_filename 
                  << " : " << image.geterror() << "\n";
}



static void
test_shadow (ustring filename)
{
}



static void
test_environment (ustring filename)
{
}



static void
test_getimagespec_gettexels (ustring filename)
{
    ImageSpec spec;
    if (! texsys->get_imagespec (filename, spec)) {
        std::cerr << "Could not get spec for " << filename << "\n";
        std::string e = texsys->geterror ();
        if (! e.empty())
            std::cerr << "ERROR: " << e << "\n";
        return;
    }
    int w = spec.width/2, h = spec.height/2;
    ImageSpec postagespec (w, h, spec.nchannels, TypeDesc::FLOAT);
    ImageBuf buf ("postage.exr", postagespec);
    TextureOptions opt;
    opt.nchannels = spec.nchannels;
    if (missing[0] >= 0)
        opt.missingcolor.init ((float *)&missing, 0);
    std::vector<float> tmp (w*h*spec.nchannels);
    bool ok = texsys->get_texels (filename, opt, 0, w/2, w/2+w, h/2, h/2+h,
                                  0, 1, postagespec.format, &tmp[0]);
    if (! ok)
        std::cerr << texsys->geterror() << "\n";
    for (int y = 0;  y < h;  ++y)
        for (int x = 0;  x < w;  ++x) {
            imagesize_t offset = (y*w + x) * spec.nchannels;
            buf.setpixel (x, y, &tmp[offset]);
        }
    buf.save ();
}



int
main (int argc, const char *argv[])
{
    getargs (argc, argv);

    texsys = TextureSystem::create ();
    std::cerr << "Created texture system\n";
    texsys->attribute ("statistics:level", 2);
    texsys->attribute ("autotile", autotile);
    texsys->attribute ("automip", (int)automip);
    if (cachesize >= 0)
        texsys->attribute ("max_memory_MB", cachesize);
    if (maxfiles >= 0)
        texsys->attribute ("max_open_files", maxfiles);
    if (searchpath.length())
        texsys->attribute ("searchpath", searchpath);

    if (iters > 0) {
        ustring filename (filenames[0]);
        test_gettextureinfo (filename);
        const char *texturetype = "Plain Texture";
        texsys->get_texture_info (filename, ustring("texturetype"),
                                  TypeDesc::STRING, &texturetype);
        if (! strcmp (texturetype, "Plain Texture")) {
            test_plain_texture ();
        }
        if (! strcmp (texturetype, "Shadow")) {
            test_shadow (filename);
        }
        if (! strcmp (texturetype, "Environment")) {
            test_environment (filename);
        }
        test_getimagespec_gettexels (filename);
    }
    
    std::cout << "Memory use: "
              << Strutil::memformat (Sysutil::memory_used(true)) << "\n";
    TextureSystem::destroy (texsys);
    return 0;
}
