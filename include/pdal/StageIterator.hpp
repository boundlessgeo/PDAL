/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
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

#ifndef INCLUDED_STAGEITERATOR_HPP
#define INCLUDED_STAGEITERATOR_HPP

#include <pdal/pdal.hpp>
#include <pdal/Stage.hpp>

namespace pdal
{
class PointBuffer;



class PDAL_DLL StageIterator
{
public:
    StageIterator(const DataStage& stage);
    virtual ~StageIterator();

    const DataStage& getStage() const;

    // This reads a set of points at the current position in the file.
    //
    // The schema of the PointBuffer buffer we are given here might
    // not match our own header's schema.  That's okay, though: all
    // that matters is that the buffer we are given has the fields
    // we need to write into.
    //
    // Returns the number of valid points read.
    boost::uint32_t read(PointBuffer&);

    // Returns the current point number.  The first point is 0.
    // If this number if > getNumPoints(), then no more points
    // may be read (and atEnd() should be true).
    //
    // All stages have the notion of a current point number, even for stages
    // that are not really "oredered", in that the index just starts at zero 
    // and increments by N every time another N points are read
    boost::uint64_t getIndex() const;

    // used to control intermediate buffering needed by some stages
    void setChunkSize(boost::uint32_t size);
    boost::uint32_t getChunkSize() const;

protected:
    virtual boost::uint32_t readImpl(PointBuffer&) = 0;

    // This is provided as a sample implementation that some stages could use
    // to implement their own skip or seek functions. It uses the read() call 
    // to advance "count" points forward, so it is not at all optimal.
    boost::uint64_t naiveSkipImpl(boost::uint64_t count);

    boost::uint64_t m_index;

private:
    const DataStage& m_stage;
    boost::uint32_t m_chunkSize;

    StageIterator& operator=(const StageIterator&); // not implemented
    StageIterator(const StageIterator&); // not implemented
};


class PDAL_DLL StageSequentialIterator : public StageIterator
{
public:
    StageSequentialIterator(const DataStage& stage);
    virtual ~StageSequentialIterator();

    // advance N points ahead in the file
    //
    // In some cases, this might be a very slow, painful function to call because
    // it might entail physically reading the N points (and dropping the data on the
    // floor)
    //
    // Returns the number actually skipped (which might be less than count, if the
    // end of the stage was reached first).
    boost::uint64_t skip(boost::uint64_t count);

    // returns true after we've read all the points available to this stage
    bool atEnd() const;

protected:
    // from Iterator
    virtual boost::uint32_t readImpl(PointBuffer&) = 0;

    virtual boost::uint64_t skipImpl(boost::uint64_t pointNum) = 0;
    virtual bool atEndImpl() const = 0;
};


class PDAL_DLL StageRandomIterator : public StageIterator
{
public:
    StageRandomIterator(const DataStage& stage);
    virtual ~StageRandomIterator();

    // seek to point N (an absolute value)
    //
    // In some cases, this might be a very slow, painful function to call because
    // it might entail physically reading the N points (and dropping the data on the
    // floor)
    //
    // Returns the number actually seeked to (which might be less than asked for, if the
    // end of the stage was reached first).
    boost::uint64_t seek(boost::uint64_t position);

protected:
    // from Iterator
    virtual boost::uint64_t seekImpl(boost::uint64_t position) = 0;
};

class PDAL_DLL StageBlockIterator : public StageIterator
{
public:
    StageBlockIterator(const DataStage& stage);
    virtual ~StageBlockIterator();

protected:
    // from StageIterator
    virtual boost::uint64_t seekImpl(boost::uint64_t position) = 0;

};

} // namespace pdal

#endif