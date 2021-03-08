/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */


// We need our declaration
#include "../include/V4L2Source.hpp"

#include <sys/mman.h>
#include <sys/ioctl.h>

#define Zero(X) memset(&X, 0, sizeof(X))

int V4L2Thread::Context::ioctl(int method, void *arg) {
    
    for (int tries = IOCTLRetry; tries; tries--) {
        int ret = ::ioctl((int)fd, method, arg);
        if (!ret) return 0;
        if (errno != EINTR && errno != EAGAIN && errno != ETIMEDOUT) break;
    };

    log(Error, "Failure in IOCTL(%08X): %d:%s", method, errno, strerror(errno));
    return -1;
}

String V4L2Thread::Context::openDevice(const char * path, int preferredVideoWidth, int preferredVideoHeight)
{
    fd.Mutate(::open(path, O_RDWR));
    if (fd == -1) return String::Print("Can't open: %s", path);

    Zero(caps);
    int ret = ioctl(VIDIOC_QUERYCAP, &caps);
    if(ret < 0) return "Can't fetch video device capabilities";
    if(!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) return "Device is not a video capture";
    supportsStream = caps.capabilities & V4L2_CAP_STREAMING;
    if (!supportsStream && !(caps.capabilities & V4L2_CAP_READWRITE)) return "Device does not support streaming or read/write mode";

    // Enumerate all formats supported by the device
    struct v4l2_fmtdesc fmtdesc = {0};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool compatible = false;
    while (::ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
    {   
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG || fmtdesc.pixelformat == V4L2_PIX_FMT_JPEG) { compatible = true; break; }
        fmtdesc.index++;
    }
    if (!compatible) return "This device does not support MJPEG or JPEG pixel format";

    // Then find out the largest frame size supported for this format
    struct v4l2_frmsizeenum frmsize = {0};
    frmsize.pixel_format = fmtdesc.pixelformat;
    int maxWidth = 0, maxHeight = 0;
    while (::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0)
    {
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
        {
            if (frmsize.discrete.width > maxWidth) { maxWidth = frmsize.discrete.width; maxHeight = frmsize.discrete.height; }
        }
        else {
            if (frmsize.stepwise.max_width > maxWidth) { maxWidth = frmsize.stepwise.max_width; maxHeight = frmsize.stepwise.max_height; }
        }

        frmsize.index++;
    }

    log(Info, "Detected maximum picture size as %d x %d\n", maxWidth, maxHeight); 

    // Remember the highest resolution format for the picture
    Zero(highres);
    highres.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    highres.fmt.pix.width = maxWidth;
    highres.fmt.pix.height = maxHeight;
    highres.fmt.pix.pixelformat = fmtdesc.pixelformat;
    highres.fmt.pix.field = V4L2_FIELD_ANY;
    ret = ioctl(VIDIOC_S_FMT, &highres);
    if(ret < 0) return "Can't set format to the maximum picture size";


    // Check format
    Zero(format);
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = preferredVideoWidth;
    format.fmt.pix.height = preferredVideoHeight;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    ret = ioctl(VIDIOC_S_FMT, &format);
    if(ret < 0) 
    {
        // Try JPEG format as well before giving up
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
        ret = ioctl(VIDIOC_S_FMT, &format);
        if (ret < 0) return String::Print("Can't set JPEG or MJPEG format (w:%d, h:%d)", preferredVideoWidth, preferredVideoHeight);
    }

    // Check if the resolution was modified 
    if(format.fmt.pix.width != preferredVideoWidth || format.fmt.pix.height != preferredVideoHeight)
        log(Warning, "Resolution not supported, using w:%d, h:%d\n", format.fmt.pix.width, format.fmt.pix.height);
    
    log(Info, "Video set up for width:%d, height:%d, format:%c%c%c%c%s", format.fmt.pix.width, format.fmt.pix.height, 
                                (char)(format.fmt.pix.pixelformat & 0x7F), (char)((format.fmt.pix.pixelformat & 0x7F00)>>8), (char)((format.fmt.pix.pixelformat & 0x7F0000)>>16), (char)((format.fmt.pix.pixelformat & 0x7F000000)>>24), 
                                (format.fmt.pix.pixelformat & 0x80000000U) ? " - BigEndian" : " - LittleEndian");

    return switchRes(&format, false);

}

String V4L2Thread::Context::switchRes(struct v4l2_format * f, bool unmapFirst)
{
    if (unmapFirst) {
        // Need to find the buffer size to unmap it
        Zero(buffer);
        buffer.index = 0;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (ioctl(VIDIOC_QUERYBUF, &buffer) < 0) return String::Print("Can't query buffer %d", 0);

        for (int i = 0; i < BuffersCount; i++) {
            if (::munmap(mem[i], buffer.length)) return String::Print("Can't unmap buffer %d", i);
        }

        Zero(requestBuffers);
        requestBuffers.count = 0;
        requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requestBuffers.memory = V4L2_MEMORY_MMAP;

        if (ioctl(VIDIOC_REQBUFS, &requestBuffers) < 0)  return "Can't free video buffers";
    }

    int ret = ioctl(VIDIOC_S_FMT, f);
    if (ret < 0) return String::Print("Can't set JPEG or MJPEG format (w:%d, h:%d)", f->fmt.pix.width, f->fmt.pix.height);

    // Set up buffers now
    Zero(requestBuffers);
    requestBuffers.count = BuffersCount;
    requestBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    requestBuffers.memory = V4L2_MEMORY_MMAP;

    if (ioctl(VIDIOC_REQBUFS, &requestBuffers) < 0)  return "Can't allocate video buffers";
    
    for (int i = 0; i < BuffersCount; i++) {
        Zero(buffer);
        buffer.index = i;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (ioctl(VIDIOC_QUERYBUF, &buffer) < 0) return String::Print("Can't query buffer %d", i);

        mem[i] = ::mmap(0, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, (int)fd, buffer.m.offset);
        if (mem[i] == MAP_FAILED)   return String::Print("Memory mapping of the buffer %d failed", i);

        if (!unmapFirst) log(Debug, "Buffer %d (len: %u bytes) mapped at %p", i, buffer.length, mem[i]);
    }

    // Queue them now
    for (int i = 0; i < BuffersCount; i++) {
        Zero(buffer);
        buffer.index = i;
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (ioctl(VIDIOC_QBUF, &buffer) < 0) return String::Print("Can't queue buffer %d", i);  
    }      
    return "";
}


bool V4L2Thread::Context::switchToFullRes()
{
    struct v4l2_format f;
    memcpy(&f, &highres, sizeof(f));
    String ret = switchRes(&f);
    if (ret) {
        log(Error, "Error while switching resolution: %s", (const char*)ret);
        return false;
    }
    return true;
}

bool V4L2Thread::Context::switchToLowRes()
{
    struct v4l2_format f;
    memcpy(&f, &format, sizeof(f));
    String ret = switchRes(&f);
    if (ret) {
        log(Error, "Error while switching resolution: %s", (const char*)ret);
        return false;
    }
    return true;
}


bool V4L2Thread::Context::startStreaming()
{
    if (state != Off) return true;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret = ioctl(VIDIOC_STREAMON, &type);
    if (ret) {
        log(Error, "Can't start stream: %d (errno: %d)", ret, errno);
        return false;
    }
    state = On;
    return true;
}

bool V4L2Thread::Context::stopStreaming()
{
    if (state == Off) return true;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret = ioctl(VIDIOC_STREAMOFF, &type);
    if (ret) {
        log(Error, "Can't start stream: %d (errno: %d)", ret, errno);
        return false;
    }
    state = Off;
    return true;
}

bool V4L2Thread::Context::eventLoop()
{
    struct v4l2_event ev;
    // We don't want to block here, so switch the file descriptor to non blocking first
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int ret = ::ioctl(fd, VIDIOC_DQEVENT, &ev); // Don't retry on error and don't log error here if there's no event to fetch
    // Set it blocking again here
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    if (!ret) {
        switch (ev.type) {
        // End of stream should stop streaming thread
        case V4L2_EVENT_EOS:
            log(Info, "End of stream event");
            return false;
        // React on source change ()
        case V4L2_EVENT_SOURCE_CHANGE:
            if (ev.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) {
                log(Info, "Source changed event");
                // TODO, should enqueue a frame grab event here
                return false;
            }
            break;
        default: break;
        }
    }
    return true;
}

bool V4L2Thread::Context::fetchFrame(uint8 * & ptr, size_t & size)
{
    Zero(buffer);
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    int ret = ioctl(VIDIOC_DQBUF, &buffer);
    if(ret < 0) return false;

    ptr = (uint8*)mem[buffer.index];
    size = buffer.bytesused;
    return true;
}

bool V4L2Thread::Context::returnFrame()
{
    int ret = ioctl(VIDIOC_QBUF, &buffer);
    if (ret < 0) return false;
    return true;
}



String V4L2Thread::captureFullResPicture(Utils::MemoryBlock & block)
{

    if (fullResPic) return "ERROR: Already busy capturing";
    fullResPic = &block;
    if (!isRunning()) {
        // The thread is not running, let's capture a frame and exit
        if (!fetchFullRes()) { fullResPic = 0; return "ERROR: While fetching full resolution picture"; }
        fullResPic = 0;
        return "";        
    }

    // Else tell the thread to do it
    captureFullRes.Set();
    captureDone.Wait(Threading::TimeOut::Infinite);
    fullResPic = 0;
    return ""; // Done
}

bool V4L2Thread::fetchFullRes() 
{
    bool running = context.state == On;
    if (running && !context.stopStreaming()) return false;
    // Start the stream as full res now
    if (!context.switchToFullRes()) return false;
    // Start the stream here
    if (!context.startStreaming()) return false;

    // Capture a single frame
    uint8 * ptr = 0; size_t size = 0;
    if (!context.fetchFrame(ptr, size)) return false;
    if (!fullResPic->ensureSize(size, true)) return false;
    memcpy(fullResPic->getBuffer(), ptr, size);

    // Stop full res picture fetching now
 //   if (!context.returnFrame()) return false;
    if (!context.stopStreaming()) return false;

    // Switch back to low res now
    if (!context.switchToLowRes()) return false;
    if (running && !context.startStreaming()) return false;

    return true;
}


uint32 V4L2Thread::runThread()
{
    // Don't continue if we are not started yet
    if (context.fd == -1) return 0;

    // Ok, let's start the stream now
    if (!context.startStreaming()) return 0; // Failed.

    while (isRunning())
    {
        // Fetch pictures from the source now
        if (!context.eventLoop()) return 0;

        // Check if capture a full frame is requested
        if (captureFullRes.Wait(Threading::TimeOut::InstantCheck)) {
            // It is, let's re-initialize the camera
            bool success = fetchFullRes();
            // Don't block the main thread here
            captureDone.Set();
            // Upon any failure, we can't recover here
            if (!success) return 0;
        }

        // Fetch a frame
        uint8 * ptr = 0; size_t size = 0;
        if (!context.fetchFrame(ptr, size)) return 0;

        // Skip very small or corrupt picture here
        if (size > 200) {
            // Call the sink now
            if (!sink.pictureReceived(ptr, size)) return 0;
        }

        // Tell the context, we are done with the frame now
        if (!context.returnFrame()) return 0;
    }
    return 0;
}