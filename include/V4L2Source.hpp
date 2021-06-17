/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */


#pragma once

// We need threading code here
#include "Threading/Threads.hpp"
// We need FileIndexWrapper here
#include "Platform/Platform.hpp"
// We need Utils::MemoryBlock here
#include "Utils/MemoryBlock.hpp"

// We need logs too
#include "LogLevel.hpp"

#include <linux/types.h>          /* for videodev2.h */
#include <linux/videodev2.h>

typedef Strings::FastString String;

/** The V4L2 receiving thread */
struct V4L2Thread : public Threading::Thread
{
    // Type definition and enumerations
public:
    /** The streaming state */
    enum State {
        Off             = 0,
        On              = 1,
        Paused          = 2,
        Disconnected    = 3,
    };

    /** Some constants */
    enum Constants {
        IOCTLRetry      = 4,
        BuffersCount    = 3,
    };

    /** Exception thrown upon unexpected device disconnection */
    struct DisconnectedError {};

    /** The V4L2 context object */
    struct Context
    {
        /** The file descriptor to the object */
        Platform::FileIndexWrapper fd;

        // V4L2 specific stuff here
        struct v4l2_capability      caps;
        struct v4l2_format          format;
        struct v4l2_format          highres;
        struct v4l2_buffer          buffer;
        struct v4l2_requestbuffers  requestBuffers;
        void *                      mem[BuffersCount];

        /** Check if the device support streaming API */
        bool supportsStream; 
        /** The streaming state */
        State state;
        /** The number of pictures to drop while switching resolution */
        unsigned framesToDrop;

        // Interface
    public:
        // Low level IOCTL that's retrying upon recoverable errors
        int ioctl        (int method, void *arg, const bool throwOnDisconnect = true);

 //       int getControl   (int control);
 //       int setControl   (int control, int value, int plugin_number, globals *pglobal);
 //       int upControl    (int control);
 //       int downControl  (int control);
 //       int toggleControl(int control);
 //       int resetControl (int control);

        // Open the device and extract all useful informations
        String  openDevice(const char * path, int preferredVideoWidth = 640, int preferredVideoHeight = 480, int picWidth = 0, int picHeight = 0, unsigned stabPicCount = 0);
        // Close the device (used to release memory and the file descriptor so device can be unplugged)
        String  closeDevice();
        // Start the stream
        bool    startStreaming();
        // Stop the stream
        bool    stopStreaming();
        // The V4L2 event loop
        bool    eventLoop();
        // The frame fetching loop
        bool    fetchFrame(uint8 * & ptr, size_t & size);
        // Return the frame to the queue
        bool    returnFrame();


        // Switch to full resolution picture streaming
        bool    switchToFullRes();
        // Switch to low resolution picture streaming
        bool    switchToLowRes();

        // Helper methods
    private:
        // Switch resolution (internal implementation)
        String  switchRes(struct v4l2_format * f, bool unmapFirst = true);
        // Unmap the buffer
        String  unmapBuffers();

    public:
        Context() : fd(-1), supportsStream(false), state(Disconnected), framesToDrop(0) {}
    };

    /** The receiving interface */
    struct PictureSink
    {
        /** Called upon new picture received, return false to stop the receiving thread */
        virtual bool pictureReceived(const uint8 * data, const size_t len) = 0;
        virtual ~PictureSink() {}
    };

    virtual uint32 runThread();
    bool fetchFullRes();

    // Interface
public:
    V4L2Thread(PictureSink & sink) : 
        Threading::Thread("V4L2Thread"), sink(sink), 
        captureFullRes("FullRes", Threading::Event::AutoReset), 
        captureDone("FullResDone", Threading::Event::AutoReset), fullResPic(0) { }

    ~V4L2Thread() { 
        // Don't let the thread run here
        destroyThread();
    }

    String startV4L2Device(const char * path, int preferredVideoWidth = 640, int preferredVideoHeight = 480, int picWidth = 0, int picHeight = 0, unsigned stabPicCount = 0) {
        return context.openDevice(path, preferredVideoWidth, preferredVideoHeight, picWidth, picHeight, stabPicCount);
    }

    String captureFullResPicture(Utils::MemoryBlock & block);

    String stopV4L2Device() {
        // First stop the thread
        destroyThread();
        return context.closeDevice();
    }
 
    bool isOpened() const { return context.fd != -1; }
    bool isDevicePresent() const { return context.state != Disconnected; }

    int getLowResWidth()  const { return context.format.fmt.pix.width; }
    int getLowResHeight() const { return context.format.fmt.pix.height; }
    int getHighResWidth() const { return context.highres.fmt.pix.width; }
    int getHighResHeight() const { return context.highres.fmt.pix.height; }

    // Members
private:
    Context                 context;
    PictureSink       &     sink;
    Threading::Event        captureFullRes, captureDone;
    Utils::MemoryBlock *    fullResPic;
};
