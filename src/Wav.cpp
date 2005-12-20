/*
Copyright (c) 2005, John Hurst
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*! \file    Wav.cpp
    \version $Id$
    \brief   Wave file common elements
*/

#include <Wav.h>
#include <assert.h>


const ui32_t SimpleHeaderLength = 46;

//
ASDCP::Wav::SimpleWaveHeader::SimpleWaveHeader(ASDCP::PCM::AudioDescriptor& ADesc)
{
  format = 1;
  nchannels = ADesc.ChannelCount;
  bitspersample = ADesc.QuantizationBits;
  samplespersec = (ui32_t)ceil(ADesc.AudioSamplingRate.Quotient());
  avgbps = samplespersec * nchannels * ((bitspersample + 7) / 8);
  blockalign = nchannels * ((bitspersample + 7) / 8);
  cbsize = 0;	  
  data_len = ASDCP::PCM::CalcFrameBufferSize(ADesc) * ADesc.ContainerDuration;
}

//
void
ASDCP::Wav::SimpleWaveHeader::FillADesc(ASDCP::PCM::AudioDescriptor& ADesc, ASDCP::Rational PictureRate) const
{
  ADesc.SampleRate = PictureRate;

  ADesc.LinkedTrackID = 0;
  ADesc.Locked = 0;
  ADesc.ChannelCount = nchannels;
  ADesc.AudioSamplingRate = Rational(samplespersec, 1);
  ADesc.AvgBps = avgbps;
  ADesc.BlockAlign = blockalign;
  ADesc.QuantizationBits = bitspersample;
  ui32_t FrameBufferSize = ASDCP::PCM::CalcFrameBufferSize(ADesc);
  ADesc.ContainerDuration = data_len / FrameBufferSize;
}


//
ASDCP::Result_t
ASDCP::Wav::SimpleWaveHeader::WriteToFile(ASDCP::FileWriter& OutFile) const
{
  ui32_t write_count;
  byte_t tmp_header[SimpleHeaderLength];
  byte_t* p = tmp_header;

  static ui32_t fmt_len =
    sizeof(format)
    + sizeof(nchannels)
    + sizeof(samplespersec)
    + sizeof(avgbps)
    + sizeof(blockalign)
    + sizeof(bitspersample)
    + sizeof(cbsize);

  ui32_t RIFF_len = data_len + SimpleHeaderLength - 8;

  memcpy(p, &FCC_RIFF, sizeof(fourcc)); p += 4;
  *((ui32_t*)p) = ASDCP_i32_LE(RIFF_len); p += 4;
  memcpy(p, &FCC_WAVE, sizeof(fourcc)); p += 4;
  memcpy(p, &FCC_fmt_, sizeof(fourcc)); p += 4;
  *((ui32_t*)p) = ASDCP_i32_LE(fmt_len); p += 4;
  *((ui16_t*)p) = ASDCP_i16_LE(format); p += 2;
  *((ui16_t*)p) = ASDCP_i16_LE(nchannels); p += 2;
  *((ui32_t*)p) = ASDCP_i32_LE(samplespersec); p += 4;
  *((ui32_t*)p) = ASDCP_i32_LE(avgbps); p += 4;
  *((ui16_t*)p) = ASDCP_i16_LE(blockalign); p += 2;
  *((ui16_t*)p) = ASDCP_i16_LE(bitspersample); p += 2;
  *((ui16_t*)p) = ASDCP_i16_LE(cbsize); p += 2;
  memcpy(p, &FCC_data, sizeof(fourcc)); p += 4;
  *((ui32_t*)p) = ASDCP_i32_LE(data_len); p += 4;

  return OutFile.Write(tmp_header, SimpleHeaderLength, &write_count);
}

//
ASDCP::Result_t
ASDCP::Wav::SimpleWaveHeader::ReadFromFile(const ASDCP::FileReader& InFile, ui32_t* data_start)
{
  ui32_t read_count = 0;
  ui32_t local_data_start = 0;
  ASDCP::PCM::FrameBuffer TmpBuffer(MaxWavHeader);

  if ( data_start == 0 )
    data_start = &local_data_start;

  Result_t result = InFile.Read(TmpBuffer.Data(), TmpBuffer.Capacity(), &read_count);

  if ( ASDCP_SUCCESS(result) )
    result = ReadFromBuffer(TmpBuffer.RoData(), read_count, data_start);

    return result;
}

ASDCP::Result_t
ASDCP::Wav::SimpleWaveHeader::ReadFromBuffer(const byte_t* buf, ui32_t buf_len, ui32_t* data_start)
{
  if ( buf_len < SimpleHeaderLength )
    return RESULT_SMALLBUF;

  *data_start = 0;
  const byte_t* p = buf;
  const byte_t* end_p = p + buf_len;

  fourcc test_RIFF(p); p += 4;
  if ( test_RIFF != FCC_RIFF )
    {
      DefaultLogSink().Error("Files does not begin with RIFF header\n");      
      return RESULT_RAW_FORMAT;
    }

  ui32_t RIFF_len = ASDCP_i32_LE(*(ui32_t*)p); p += 4;

  fourcc test_WAVE(p); p += 4;
  if ( test_WAVE != FCC_WAVE )
    {
      DefaultLogSink().Error("Files does not begin with WAVE header\n");
      return RESULT_RAW_FORMAT;
    }

  fourcc test_fcc;

  while ( p < end_p )
    {
      test_fcc = fourcc(p); p += 4;
      ui32_t chunk_size = ASDCP_i32_LE(*(ui32_t*)p); p += 4;

      if ( test_fcc == FCC_data )
	{
	  if ( chunk_size > RIFF_len )
	    {
	      DefaultLogSink().Error("Chunk size %lu larger than file: %lu\n", chunk_size, RIFF_len);
	      return RESULT_RAW_FORMAT;
	    }

	  data_len = chunk_size;
	  *data_start = p - buf;
	  break;
	}

      if ( test_fcc == FCC_fmt_ )
	{
	  ui16_t format = ASDCP_i16_LE(*(ui16_t*)p); p += 2;

	  if ( format != 1 )
	    {
	      DefaultLogSink().Error("Expecting uncompressed essence, got format type %hu\n", format);
	      return RESULT_RAW_FORMAT;
	    }

	  nchannels = ASDCP_i16_LE(*(ui16_t*)p); p += 2;
	  samplespersec = ASDCP_i32_LE(*(ui32_t*)p); p += 4;
	  avgbps = ASDCP_i32_LE(*(ui32_t*)p); p += 4;
	  blockalign = ASDCP_i16_LE(*(ui16_t*)p); p += 2;
	  bitspersample = ASDCP_i16_LE(*(ui16_t*)p); p += 2;
	  p += chunk_size - 16;
	}
      else
	{
	  p += chunk_size;
	}
    }

  if ( *data_start == 0 ) // can't have no data!
    {
      DefaultLogSink().Error("No data chunk found, file contains no essence\n");
      return RESULT_RAW_FORMAT;
    }

  return RESULT_OK;
}



//
// end Wav.cpp
//
