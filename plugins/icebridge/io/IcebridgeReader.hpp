/******************************************************************************
* Copyright (c) 2014, Connor Manning, connor@hobu.co
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#pragma once

#include <pdal/Reader.hpp>
#include <pdal/Options.hpp>
#include <pdal/StageFactory.hpp>

#include "Hdf5Handler.hpp"

#include <vector>

namespace pdal
{

class icebridge_error : public pdal_error
{
public:
    icebridge_error(std::string const& msg) : pdal_error(msg)
    { }
};

class PDAL_DLL IcebridgeReader : public pdal::Reader
{
public:
    SET_STAGE_NAME("readers.icebridge", "Icebridge Reader")
    SET_STAGE_LINK("http://pdal.io/stages/readers.icebridge.html")
    SET_STAGE_ENABLED(true)

    IcebridgeReader() : pdal::Reader()
        {}

    static Options getDefaultOptions();
    static Dimension::IdList getDefaultDimensions();

private:
    Hdf5Handler m_hdf5Handler;
    point_count_t m_index;

    virtual void addDimensions(PointContextRef ctx);
    virtual void ready(PointContextRef ctx);
    virtual point_count_t read(PointBuffer& data, point_count_t count);
    virtual void done(PointContextRef ctx);
    virtual bool eof();

    IcebridgeReader& operator=(const IcebridgeReader&);   // Not implemented.
    IcebridgeReader(const IcebridgeReader&);              // Not implemented.
};

} // namespace pdal
