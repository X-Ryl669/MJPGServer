/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */

// We need our own include
#include "../include/MJPGServer.hpp"


// We need arguments parser for the command line interface
#include "Platform/Arguments.hpp"
#include "Platform/Platform.hpp"
// We need file function too
#include "File/File.hpp"



bool exitRequired = false;
void asyncProcess(int signal)
{
    static const char stopping[] = "\n|  Stopping, please wait...  |\n";
    switch (signal)
    {
    case SIGINT: exitRequired = true; write(2, stopping, sizeof(stopping)); fsync(2); return;
    default: return;
    }
}


int logLevel = LogLevel::Info;

int main(int argc, const char ** argv)
{
    signal(SIGINT, asyncProcess);

    bool daemonize = false;
    unsigned  port = 8080;
    String  devPath = "/dev/video0";
    int lowResWidth = 640, lowResHeight = 480; 

    // Options
    Arguments::declare(daemonize,           "Daemonize the server", "daemon", "d");
    Arguments::declare(port,                "The port to listen on", "port", "p");
    Arguments::declare(devPath,             "Path to the device to use", "camera", "c");
    Arguments::declare(logLevel,            "Log level (-1: debug, 0: Info, 2: Error, 3: Silent)", "loglevel", "v");
    Arguments::declare(lowResWidth,         "The low resolution picture width (default 640px)", "width", "w");
    Arguments::declare(lowResHeight,        "The low resolution picture height (default 480px)", "height", "t");
   

    fprintf(stdout, "Thank you for using MJPGServer\n");

    String error = Arguments::Core::parse(argc, argv);
    if (error) return log(Error, "%s", (const char*)error);

    if (daemonize) {
        bool parent = false;
        if (!Platform::daemonize("/var/run/mjpgsrv.pid", "mjpgsrv", parent))
            return log(Error, "Can't daemonize");
        if (parent) _exit(0);

        // Now we are the child daemon, we'll need to drop priviledges ASAP
    }

    MJPGServer srv;
    error = srv.startV4L2Device(devPath, lowResWidth, lowResHeight);
    if (error) return log(Error, "%s\n", (const char*)error);

    error = srv.startServer((uint16)min(port, 65535U));
    if (error) return log(Error, "%s\n", (const char*)error);

    Platform::dropPrivileges(); // We don't need any priviledge anymore here, since we have opened the server socket and camera device already
    while (!exitRequired && srv.loop()) {}

    srv.stopServer();
    return 0;
}