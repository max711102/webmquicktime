// Copyright (c) 2010 The WebM project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#define HAVE_CONFIG_H "vpx_codecs_config.h"
#include "vpx/vpx_encoder.h"
#include "vpx/vpx_codec_impl_top.h"
#include "vpx/vpx_codec_impl_bottom.h"
#include "vpx/vp8cx.h"

#if __APPLE_CC__
#include <QuickTime/QuickTime.h>
#else
#include <ConditionalMacros.h>
#include <Endian.h>
#include <ImageCodec.h>
#endif

#include "log.h"
#include "Raw_debug.h"


#include "VP8CodecVersion.h"
#include "VP8Encoder.h"
#include "VP8EncoderEncode.h"
#include "VP8EncoderGui.h"

static void setCustom(VP8EncoderGlobals glob);
static ComponentResult setMaxKeyDist(VP8EncoderGlobals glob);
static ComponentResult setFrameRate(VP8EncoderGlobals glob);
static ComponentResult setBitrate(VP8EncoderGlobals glob,
                                  ICMCompressorSourceFrameRef sourceFrame);
static void setUInt(unsigned int * i, UInt32 val);
static void setCustom(VP8EncoderGlobals glob);
static void initializeCodec(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);
static ComponentResult convertColorSpace(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);

//these are for the source frame queue
static void addSourceFrame(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);
static ICMCompressorSourceFrameRef popSourceFrame(VP8EncoderGlobals glob);
static Boolean isInQueue(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame);

static void dbg_printEncoderSettings(vpx_codec_enc_cfg_t  *cfg)
{
  dbg_printf("[vp8e] ------- dbg_printEncoderSettings\n");
  dbg_printf("g_usage                     %d\n", cfg->g_usage);
  dbg_printf("g_threads                   %d\n", cfg->g_threads);
  dbg_printf("g_profile                   %d\n", cfg->g_profile);
  dbg_printf("g_w                         %d\n", cfg->g_w);
  dbg_printf("g_h                         %d\n", cfg->g_h);
  dbg_printf("g_timebase                  %d/%d\n", cfg->g_timebase.num, cfg->g_timebase.den);
  dbg_printf("g_error_resilient           %d\n", cfg->g_error_resilient);
  dbg_printf("g_pass                      %d\n", cfg->g_pass);
  dbg_printf("g_lag_in_frames             %d\n", cfg->g_lag_in_frames);
  dbg_printf("rc_dropframe_thresh         %d\n", cfg->rc_dropframe_thresh);
  dbg_printf("rc_resize_allowed           %d\n", cfg->rc_resize_allowed);
  dbg_printf("rc_resize_up_thresh         %d\n", cfg->rc_resize_up_thresh);
  dbg_printf("rc_resize_down_thresh       %d\n", cfg->rc_resize_down_thresh);
  dbg_printf("rc_resize_down_thresh       %d\n", cfg->rc_resize_down_thresh);
  dbg_printf("rc_end_usage                %d\n", cfg->rc_end_usage);
  dbg_printf("rc_twopass_stats_in         %d\n", cfg->rc_twopass_stats_in);
  dbg_printf("rc_target_bitrate           %d\n", cfg->rc_target_bitrate);
  dbg_printf("rc_min_quantizer            %d\n", cfg->rc_min_quantizer);
  dbg_printf("rc_max_quantizer            %d\n", cfg->rc_max_quantizer);
  dbg_printf("rc_undershoot_pct           %d\n", cfg->rc_undershoot_pct);
  dbg_printf("rc_overshoot_pct            %d\n", cfg->rc_overshoot_pct);
  dbg_printf("rc_buf_sz                   %d\n", cfg->rc_buf_sz);
  dbg_printf("rc_buf_initial_sz           %d\n", cfg->rc_buf_initial_sz);
  dbg_printf("rc_buf_optimal_sz           %d\n", cfg->rc_buf_optimal_sz);
  dbg_printf("rc_2pass_vbr_bias_pct       %d\n", cfg->rc_2pass_vbr_bias_pct);
  dbg_printf("rc_2pass_vbr_minsection_pct %d\n", cfg->rc_2pass_vbr_minsection_pct);
  dbg_printf("rc_2pass_vbr_maxsection_pct %d\n", cfg->rc_2pass_vbr_maxsection_pct);
  dbg_printf("kf_mode                     %d\n", cfg->kf_mode);
  dbg_printf("kf_min_dist                 %d\n", cfg->kf_min_dist);
  dbg_printf("kf_max_dist                 %d\n", cfg->kf_max_dist);
}

static ComponentResult emitEncodedFrame(VP8EncoderGlobals glob, const vpx_codec_cx_pkt_t *pkt)
{
  ICMMutableEncodedFrameRef encodedFrame = NULL;
  unsigned char *dataPtr;
  size_t dataSize = 0;
  ComponentResult err= noErr;
  MediaSampleFlags mediaSampleFlags;

  Boolean keyFrame = false;
  Boolean droppableFrame = false;
  Boolean invisibleFrame = false;

  //get the source frame off the queue
  //for an alt-ref frame this won't work.
  ICMCompressorSourceFrameRef sourceFrame = NULL;
  dbg_printf("[VP8E] emitEncodedFrame frames out %ld, pts %ld,  frames in queue %d\n",
             glob->sourceQueue.frames_out, pkt->data.frame.pts, glob->sourceQueue.size);

  if (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE)
  {
    dbg_printf("[VP8E] Keeping Altref Frame data %ld\n", pkt->data.frame.sz);
    //This indicates an alt -ref frame, instead of popping the frame I'm just going to hold on to data and append.
    if (glob->altRefFrame.buf != NULL)
    {
      //this shouldn't happen
      dbg_printf("[VP8E]  Errror: possible memory leak in alt ref frame data\n");
    }
    glob->altRefFrame.buf = malloc(pkt->data.frame.sz);
    memcpy(glob->altRefFrame.buf, pkt->data.frame.buf, pkt->data.frame.sz);
    glob->altRefFrame.size = pkt->data.frame.sz;
    return noErr;
  }


  while (glob->sourceQueue.size > 0)
  {
    //time is in timeBase *2
    UInt32 expectedTime = glob->sourceQueue.frames_out  * 2 ;
    dbg_printf("Expected time = %lu\n", expectedTime);
    sourceFrame = popSourceFrame(glob);
    if (expectedTime >= pkt->data.frame.pts)
    {
      break;
    }
    else //frames_out < pts
    {
      dbg_printf("[VP8E] Dropping frame %ld, Queue size  %ld\n",
                 glob->sourceQueue.frames_out, glob->sourceQueue.size);
      ICMCompressorSessionDropFrame(glob->session, sourceFrame);
    }
    sourceFrame = NULL;
  }
  if (sourceFrame == NULL && (pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE) == 0)
  {
    dbg_printf("[VP8e] Error: asked to output a frame at time %ld but last frame in was %ld\n",
               pkt->data.frame.pts, glob->sourceQueue.frames_in);
    return qErr;
  }
  TimeValue64 frameDisplayDuration = 0;
  TimeScale timescale = 0;
  ICMValidTimeFlags validTimeFlags = 0;
  unsigned int bitrate = 0;
  TimeValue64 timeStamp=0,decodeTimeStamp=0;
  TimeScale timeScale =0;

  //use the time stamp of the input frame for the output frame
  err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(sourceFrame, &timeStamp,NULL, &timeScale, NULL);
  decodeTimeStamp = timeStamp;
  dbg_printf("[VP8e] ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(%lld,%lld)\n" ,timeStamp, timeScale );

  err = ICMEncodedFrameCreateMutable(glob->session, sourceFrame,
                                     glob->maxEncodedDataSize, &encodedFrame);
  if (err) goto bail;
  dataPtr = ICMEncodedFrameGetDataPtr(encodedFrame);
  dbg_printf("[vp8e - %08lx] getDataPtr %x\n", (UInt32)glob, dataPtr);

  //paranoid check to make sure I don't write past my buffer
  if (pkt->data.frame.sz + dataSize >=  glob->maxEncodedDataSize)
  {
    dbg_printf("[vp8e - %08lx] Error: buffer overload.  Encoded frame larger than raw frame\n", (UInt32)glob);
    goto bail;
  }

  //if we have an altref frame, prepend that data.
  if (glob->altRefFrame.buf != NULL)
  {
    //This method prepends the altref size and altref data to the following interframe.
    // I've also tried sending in bogus source frames, and sending back altref via those (has major implementation issues)
    // Also I've tried reading altref size from the data... Seems as though I can't though in the future that may be better.
    //Quicktime and altref frames are difficult because of paramErr failures that occur when trying to use sourceframes 2x

    memcpy(&(dataPtr[dataSize]), &glob->altRefFrame.size, sizeof(glob->altRefFrame.size));
    dataSize += sizeof(glob->altRefFrame.size);
    dbg_printf("[VP8e] Appended altrefsize %ld bytes %lu\n", glob->altRefFrame.size, dataSize);

    memcpy(&(dataPtr[dataSize]), glob->altRefFrame.buf, glob->altRefFrame.size);
    dataSize += glob->altRefFrame.size;
    dbg_printf("[VP8e] Appended altref frame bytes %lu\n", dataSize);
  }

  dbg_printf("[vp8e - %08lx] copying %d bytes of data to output dataBuffer\n", (UInt32)glob, pkt->data.frame.sz);
  memcpy(&(dataPtr[dataSize]), pkt->data.frame.buf, pkt->data.frame.sz);
  dataSize += pkt->data.frame.sz;

  dbg_printf("[vp8e - %08lx]  Encoded frame %d with %d bytes of data\n", (UInt32)glob, glob->frameCount, dataSize);

  keyFrame = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
  dbg_printf(keyFrame ? "Key Packet\n" : "Non Key Packet\n");
  droppableFrame = pkt->data.frame.flags & VPX_FRAME_IS_DROPPABLE;
  dbg_printf(droppableFrame ? "Droppable frame\n" : "Not droppable frame\n");
  invisibleFrame = pkt->data.frame.flags & VPX_FRAME_IS_INVISIBLE;

  // Update the encoded frame to reflect the actual frame size, sample flags and frame type.
  err = ICMEncodedFrameSetDataSize(encodedFrame, dataSize);

  if (err) goto bail;

  mediaSampleFlags = 0;

  if (! keyFrame)
  {
    mediaSampleFlags |= mediaSampleNotSync;

    if (droppableFrame)
      mediaSampleFlags |= mediaSampleDroppable;
  }

  ICMFrameType frameType = keyFrame ? kICMFrameType_I : kICMFrameType_P;
  if (glob->altRefFrame.buf != NULL)
  {
    frameType = kICMFrameType_Unknown;
    free(glob->altRefFrame.buf);
    glob->altRefFrame.buf =NULL;
    glob->altRefFrame.size =0;
    dbg_printf("[vp8e - %08lx] frame type set to Unknown\n", (UInt32)glob);
  }
  else
    dbg_printf("[vp8e - %08lx] frame type set to %c\n", (UInt32)glob, keyFrame ? 'I' : 'P');

  if (kICMFrameType_I == frameType)
    mediaSampleFlags |= mediaSampleDoesNotDependOnOthers;

  err = ICMEncodedFrameSetMediaSampleFlags(encodedFrame, mediaSampleFlags);

  if (err)
    goto bail;

  err = ICMEncodedFrameSetFrameType(encodedFrame, frameType);
  if (err)
    goto bail;

  if (timeStamp == 0)
  {
    timeStamp = pkt->data.frame.pts * glob->cfg.g_timebase.num * timeScale / 2 /glob->cfg.g_timebase.den ;  //pts is timebase *2 2
  }

  dbg_printf("[vp8e]setting ICMEncodedFrameSetDisplayTimeStamp %lld\n", timeStamp);
  err= ICMEncodedFrameSetDisplayTimeStamp(encodedFrame, timeStamp);

  // Output the encoded frame.
  dbg_printf("[vp8e - %08lx]  Emit Encoded Frames\n", (UInt32)glob);
  err = ICMCompressorSessionEmitEncodedFrame(glob->session, encodedFrame, 1, &sourceFrame);

  if (err)
    goto bail;

bail:
  // Since we created this, we must also release it.
  if (encodedFrame)
  {
    ICMEncodedFrameRelease(encodedFrame);
  }
  if (err)
    dbg_printf("[VP8e] EmitEncodedFrame Error = %d\n", err);
  return err;
}


ComponentResult completeThisSourceFrame(VP8EncoderGlobals glob,
                                      ICMCompressorSourceFrameRef sourceFrame)
{
  ComponentResult err = noErr;

  if (!isInQueue(glob, sourceFrame))
  {
    err = encodeThisSourceFrame(glob, sourceFrame);
  }
  if (err) return err;
  while (isInQueue(glob, sourceFrame))
  {
    UInt32 framesInQueue = glob->sourceQueue.size;
    err = encodeThisSourceFrame(glob, NULL);
    if (err) break;

    //handles the case where terminating source frames are dropped
    if (glob->sourceQueue.size == framesInQueue)
    {
        dbg_printf("[VP8E] Dropping frame %ld\n", glob->sourceQueue.frames_out);
        sourceFrame = popSourceFrame(glob);
        err = ICMCompressorSessionDropFrame(glob->session, sourceFrame);
      if (err)
        return err;
    }
  }
  return err;
}

ComponentResult encodeThisSourceFrame(VP8EncoderGlobals glob,
                                      ICMCompressorSourceFrameRef sourceFrame)
{
  vpx_codec_err_t codecError;
  ComponentResult err = noErr;
  const UInt8 *decoderDataPtr;
  int storageIndex = 0;

  //time is multiplied by 2 to allow space for altref frames
  UInt32 time2 = glob->frameCount * 2;
  dbg_printf("[vp8e - %08lx] encode this frame %08lx %ld  time2 %lu\n",
             (UInt32)glob, (UInt32)sourceFrame, glob->frameCount, time2);

  //long dispNumber = ICMCompressorSourceFrameGetDisplayNumber(sourceFrame);

  // Initialize codec if needed
  initializeCodec(glob, sourceFrame);

  ///////         Transfer the current frame to glob->raw
  if (sourceFrame != NULL)
  {
    if (glob->currentPass != VPX_RC_FIRST_PASS)
      addSourceFrame(glob,sourceFrame);
    err = convertColorSpace(glob, sourceFrame);
    if (err) goto bail;
    int flags = 0 ; //TODO - find out what I may need in these flags
    dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec %x  raw %x framecount %d  flags %x\n", (UInt32)glob, glob->codec, glob->raw, glob->frameCount,  flags);
    //TODO seems like quality should be an option.  Right now hardcoded to GOOD_QUALITY
    codecError = vpx_codec_encode(glob->codec, glob->raw, time2,
                                  1, flags, VPX_DL_GOOD_QUALITY);
    dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec exit\n", (UInt32)glob);
  }
  else  //sourceFrame is Null. this could be termination of a pass
  {
    int flags = 0 ; //TODO - find out what I may need in these flags
    dbg_printf("[vp8e - %08lx]  vpx_codec_encode codec %x  raw %x framecount %d ----NULL TERMINATION\n", (UInt32)glob, glob->codec, NULL, glob->frameCount,  flags);
    codecError = vpx_codec_encode(glob->codec, NULL, time2,
                                  1, flags, VPX_DL_GOOD_QUALITY);
  }
  glob->frameCount++ ;  //framecount gets reset on a new pass

  if (codecError)
  {
    const char *detail = vpx_codec_error_detail(glob->codec);
    dbg_printf("[vp8e - %08lx]  error vpx encode is %s\n", (UInt32)glob, vpx_codec_error(glob->codec));

    if (detail)
      dbg_printf("    %s\n", detail);

    goto bail;
  }


  vpx_codec_iter_t iter = NULL;
  int got_data = 0;

  while (1)
  {
    const vpx_codec_cx_pkt_t *pkt = vpx_codec_get_cx_data(glob->codec, &iter);

    if (pkt == NULL)
      break;

    got_data ++;

    switch (pkt->kind)
    {
      case VPX_CODEC_CX_FRAME_PKT:
        err = emitEncodedFrame(glob, pkt);
        if (err)
          goto bail;
        break;
      case VPX_CODEC_STATS_PKT:
        if (1)
        {
          unsigned long newSize = glob->stats.sz + pkt->data.twopass_stats.sz;
          glob->stats.buf = realloc(glob->stats.buf, newSize);
          if (!glob->stats.buf)
            return mFulErr;
          dbg_printf("[vp8e - %08lx] Reallocation buffer size to %ld\n", (UInt32)glob, newSize);
          memcpy((char*)glob->stats.buf + glob->stats.sz, pkt->data.twopass_stats.buf,
                 pkt->data.twopass_stats.sz);
          glob->stats.sz = newSize;
        }
        break;

      default:
        break;
    }
  }

  if (glob->currentPass == VPX_RC_FIRST_PASS)
  {
    //in the first pass no need to export any frames
    return err;
  }

bail:
  if (err)
    dbg_printf("[vp8e - %08lx]  bailed with err %d\n", (UInt32)glob, err);

  return err;
}

//initialize the codec if needed
static void initializeCodec(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{
  if (glob->codec != NULL)
    return;
  dbg_printf("[vp8e - %08lx] initializeCodec\n", (UInt32)glob);
  glob->codec = calloc(1, sizeof(vpx_codec_ctx_t));
  setBitrate(glob, sourceFrame); //because we don't know framerate untile we have a source image.. this is done here
  setMaxKeyDist(glob);
  setFrameRate(glob);
  setCustom(glob);
  glob->cfg.g_pass = glob->currentPass;

  dbg_printEncoderSettings(&glob->cfg);
  if (vpx_codec_enc_init(glob->codec, &vpx_codec_vp8_cx_algo, &glob->cfg, 0))
  {
    const char *detail = vpx_codec_error_detail(glob->codec);
    dbg_printf("[vp8e - %08lx] Failed to initialize encoder pass = %d %s\n", (UInt32)glob, glob->currentPass, detail);
  }
  setCustomPostInit(glob);
}


//creates raw yv12 from sourceframe
static ComponentResult convertColorSpace(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{
  CVPixelBufferRef sourcePixelBuffer = NULL;
  sourcePixelBuffer = ICMCompressorSourceFrameGetPixelBuffer(sourceFrame);
  CVPixelBufferLockBaseAddress(sourcePixelBuffer, 0);
  //copy our frame to the raw image.  TODO: I'm not checking for any padding here.
  unsigned char *srcBytes = CVPixelBufferGetBaseAddress(sourcePixelBuffer);
  dbg_printf("[vp8e - %08lx] CVPixelBufferGetBaseAddress %x\n", (UInt32)glob, sourcePixelBuffer);
  dbg_printf("[vp8e - %08lx] CopyChunkyYUV422ToPlanarYV12 %dx%d, %x, %d, %x, %d, %x, %d, %x, %d \n", (UInt32)glob,
             glob->width, glob->height,
             CVPixelBufferGetBaseAddress(sourcePixelBuffer),
             CVPixelBufferGetBytesPerRow(sourcePixelBuffer),
             glob->raw->planes[PLANE_Y],
             glob->raw->stride[PLANE_Y],
             glob->raw->planes[PLANE_U],
             glob->raw->stride[PLANE_U],
             glob->raw->planes[PLANE_V],
             glob->raw->stride[PLANE_V]);
  ComponentResult err = CopyChunkyYUV422ToPlanarYV12(glob->width, glob->height,
                                                     CVPixelBufferGetBaseAddress(sourcePixelBuffer),
                                                     CVPixelBufferGetBytesPerRow(sourcePixelBuffer),
                                                     glob->raw->planes[PLANE_Y],
                                                     glob->raw->stride[PLANE_Y],
                                                     glob->raw->planes[PLANE_U],
                                                     glob->raw->stride[PLANE_U],
                                                     glob->raw->planes[PLANE_V],
                                                     glob->raw->stride[PLANE_V]);

  CVPixelBufferUnlockBaseAddress(sourcePixelBuffer, 0);
  dbg_printf("[vp8e - %08lx]  CVPixelBufferUnlockBaseAddress %x\n", sourcePixelBuffer);

  return err;
}

///////////Functions for configuring the encoder

static ComponentResult setMaxKeyDist(VP8EncoderGlobals glob)
{
  SInt32 maxInterval = 0;
  ComponentResult err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                                                                kQTPropertyClass_ICMCompressionSessionOptions,
                                                                kICMCompressionSessionOptionsPropertyID_MaxKeyFrameInterval,
                                                                sizeof(SInt32),
                                                                &maxInterval, NULL);

  if (err) return err;

  if (maxInterval == 0)
    glob->cfg.kf_max_dist = 300;  //default : don't pass in 0 as vp8 sdk reserves that for key every frame
  else
    glob->cfg.kf_max_dist = maxInterval;

  dbg_printf("[vp8e - %08lx] setMaxKeyDist %ld\n", (UInt32)glob, glob->cfg.kf_max_dist);
  return noErr;
}

static ComponentResult setFrameRate(VP8EncoderGlobals glob)
{
  dbg_printf("[vp8e - %08lx] setFrameRate \n", (UInt32) glob);
  ComponentResult err = noErr;
  Fixed frameRate;
  err= ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                                               kQTPropertyClass_ICMCompressionSessionOptions,
                                               kICMCompressionSessionOptionsPropertyID_ExpectedFrameRate,
                                               sizeof(Fixed), &frameRate, NULL);
  if (err) goto bail;
  double fps = FixedToFloat(frameRate);
  dbg_printf("[vp8e - %08lx] Got FrameRate %f \n", (UInt32) glob,fps);
  if (fps == 0)
    return err; //use the defaults
  if (fps > 29.965 && fps < 29.975) // I am putting everything in this threshold as 29.97
  {
    glob->cfg.g_timebase.num = 1001;
    glob->cfg.g_timebase.den = 30000;
  }
  else
  {
    //I'm using a default of a millisecond timebase, this may not be 100% accurate
    // however, container uses this timebase so this estimate is best.
    glob->cfg.g_timebase.num = 1000;
    glob->cfg.g_timebase.den = fps * 1000;
  }
  dbg_printf("[vp8e - %08lx] Setting g_timebase to %d/%d \n", (UInt32) glob,
             glob->cfg.g_timebase.num, glob->cfg.g_timebase.den);

bail:
  return err;
}

static ComponentResult setBitrate(VP8EncoderGlobals glob,
                                  ICMCompressorSourceFrameRef sourceFrame)
{
  ComponentResult err = noErr;
  TimeValue64 frameDisplayDuration = 0;
  TimeScale timescale = 0;
  ICMValidTimeFlags validTimeFlags = 0;
  unsigned int bitrate = 0;

  // If we have a known frame duration and an average data rate, use them to guide the byte budget.
  err = ICMCompressorSourceFrameGetDisplayTimeStampAndDuration(sourceFrame, NULL, &frameDisplayDuration,
                                                               &timescale, &validTimeFlags);
  /* Update the default configuration with our settings */
  SInt32 avgDataRate = 0;
  err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                                                kQTPropertyClass_ICMCompressionSessionOptions,
                                                kICMCompressionSessionOptionsPropertyID_AverageDataRate,
                                                sizeof(avgDataRate), &avgDataRate, NULL);

  if (avgDataRate != 0 )
  {
    //convert from bytes/sec to kilobits/second
    bitrate = avgDataRate * 8 /1000;
    dbg_printf("[vp8e - %08lx] setting bitrate to %d (from averageDataRate)\n", (UInt32)glob, bitrate);
  }
  else
  {
    CodecQ sliderVal = codecNormalQuality; //min = 0 max = 1024
    err = ICMCompressionSessionOptionsGetProperty(glob->sessionOptions,
                                                  kQTPropertyClass_ICMCompressionSessionOptions,
                                                  kICMCompressionSessionOptionsPropertyID_Quality,
                                                  sizeof(CodecQ), &sliderVal, NULL);

    if (err) return err;

    if (sliderVal > codecMaxQuality)
        sliderVal = codecMaxQuality;
    //TODO Look over this formula
      //slider val is between 0 and 1024.  This formula ranges from, 10 - 1034 kbps on a 640x480 video
    double totalPixels = glob->width * glob->height;
    double tmp = totalPixels * sliderVal / (640.0 * 480.0);
    bitrate = tmp+ 10;
    dbg_printf("[vp8e - %08lx] setting bitrate to %d (calculated from Quality Slider)\n", (UInt32)glob, bitrate);
  }

  glob->cfg.rc_target_bitrate = bitrate;
}


static void setUInt(unsigned int * i, UInt32 val)
{
  if (val == UINT_MAX)
    return;
  dbg_printf("[VP8e] setting custom setting to %lu\n",  val);
  *i= val;
}

static void setCustom(VP8EncoderGlobals glob)
{
  setUInt(&glob->cfg.g_threads, glob->settings[2]);
  setUInt(&glob->cfg.g_error_resilient, glob->settings[3]);
  setUInt(&glob->cfg.rc_dropframe_thresh, glob->settings[4]);
  if(glob->settings[5] == 1)
    glob->cfg.rc_end_usage = VPX_CBR;
  else if (glob->settings[5] ==2)
    glob->cfg.rc_end_usage = VPX_VBR;
  setUInt(&glob->cfg.g_lag_in_frames, glob->settings[6]);
  if (glob->settings[6] != UINT_MAX)
  {
    dbg_printf("[WebM] setting g_lag_in_frames to %d\n", glob->settings[6]);
  }

  setUInt(&glob->cfg.rc_min_quantizer, glob->settings[8]);
  setUInt(&glob->cfg.rc_max_quantizer, glob->settings[9]);
  setUInt(&glob->cfg.rc_undershoot_pct, glob->settings[10]);
  setUInt(&glob->cfg.rc_overshoot_pct, glob->settings[11]);

  setUInt(&glob->cfg.rc_resize_allowed, glob->settings[16]);
  setUInt(&glob->cfg.rc_resize_up_thresh, glob->settings[17]);
  setUInt(&glob->cfg.rc_resize_down_thresh, glob->settings[18]);
  setUInt(&glob->cfg.rc_buf_sz, glob->settings[19]);
  setUInt(&glob->cfg.rc_buf_initial_sz, glob->settings[20]);
  setUInt(&glob->cfg.rc_buf_optimal_sz, glob->settings[21]);

  if(glob->settings[22] == 1)
    glob->cfg.kf_mode = VPX_KF_DISABLED;
  if(glob->settings[22] == 2)
    glob->cfg.kf_mode = VPX_KF_AUTO;

  setUInt(&glob->cfg.kf_min_dist, glob->settings[23]);
  setUInt(&glob->cfg.kf_max_dist, glob->settings[24]);

  setUInt(&glob->cfg.rc_2pass_vbr_bias_pct, glob->settings[30]);
  setUInt(&glob->cfg.rc_2pass_vbr_minsection_pct, glob->settings[31]);
  setUInt(&glob->cfg.rc_2pass_vbr_maxsection_pct, glob->settings[32]);

}

void setCustomPostInit(VP8EncoderGlobals glob)
{
  if (glob->settings[12] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_CPUUSED, glob->settings[12]);
  if (glob->settings[13] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_NOISE_SENSITIVITY, glob->settings[13]);
  if (glob->settings[14] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_SHARPNESS, glob->settings[14]);
  if (glob->settings[15] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_STATIC_THRESHOLD, glob->settings[15]);
  //setUIntPostInit(glob, VP8E_SET_TOKEN_PARTITIONS, 25);  // TODO not sure how to set this.

  //TODO verify this when enabling alt - ref
  if (glob->settings[25] != UINT_MAX)
  {
    vpx_codec_control(glob->codec, VP8E_SET_ENABLEAUTOALTREF, glob->settings[25]);
    dbg_printf("[VP8e] Setting enable Altref %d\n", glob->settings[25]);
  }
  if (glob->settings[26] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_ARNR_MAXFRAMES, glob->settings[26]);
  if (glob->settings[27] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_ARNR_STRENGTH, glob->settings[27]);
  if (glob->settings[28] != UINT_MAX)
    vpx_codec_control(glob->codec, VP8E_SET_ARNR_TYPE, glob->settings[28]);
}


#define SFQ_INC_SIZE 10
//The source frame queue is maintained so we can match output packets to source frames in the queue
static void addSourceFrame(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{

  if (glob->sourceQueue.size +1 > glob->sourceQueue.max)
  {
    glob->sourceQueue.max += SFQ_INC_SIZE;
    glob->sourceQueue.queue = realloc(glob->sourceQueue.queue, glob->sourceQueue.max * sizeof(ICMCompressorSourceFrameRef));
  }
  glob->sourceQueue.queue[glob->sourceQueue.size] = sourceFrame;
  glob->sourceQueue.size += 1;
  glob->sourceQueue.frames_in += 1;
}

static ICMCompressorSourceFrameRef popSourceFrame(VP8EncoderGlobals glob)
{
  if (glob->sourceQueue.size <=0)
  {
    dbg_printf("[VP8E] **ERROR in source frame queue! Popping a frame that doesn't exist!\n");
    return NULL;
  }
  ICMCompressorSourceFrameRef rval = glob->sourceQueue.queue[0];
  int i;
  for (i=1;i < glob->sourceQueue.size; i ++)
  {
    glob->sourceQueue.queue[i-1] = glob->sourceQueue.queue[i];
  }
  glob->sourceQueue.size -=1;
  glob->sourceQueue.frames_out += 1;
  return rval;
}

static Boolean isInQueue(VP8EncoderGlobals glob, ICMCompressorSourceFrameRef sourceFrame)
{
  int i;
  for (i=0;i < glob->sourceQueue.size; i ++)
  {
    if (glob->sourceQueue.queue[i] == sourceFrame)
      return true;
  }
  return false;
}

