// We need our own include
#include "../include/MJPGServer.hpp"


// We need arguments parser for the command line interface
#include "Platform/Arguments.hpp"
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


int main(int argc, const char ** argv)
{
    signal(SIGINT, asyncProcess);

    bool daemonize = false;
    unsigned  port = 8080;
    String  devPath = "/dev/video0";

    // Options
    Arguments::declare(daemonize,           "Daemonize the server", "daemon", "d");
    Arguments::declare(port,                "The port to listen on", "port", "p");
    Arguments::declare(devPath,             "Path to the device to use", "camera", "c");

    fprintf(stdout, "Thank you for using MJPGServer\n");

    String error = Arguments::Core::parse(argc, argv);
    if (error) return fprintf(stderr, "%s\n", (const char*)error);

    MJPGServer srv;
    error = srv.startV4L2Device(devPath);
    if (error) return fprintf(stderr, "%s\n", (const char*)error);

    error = srv.startServer((uint16)min(port, 65535U));
    if (error) return fprintf(stderr, "%s\n", (const char*)error);

    while (!exitRequired && srv.loop()) {}

    srv.stopServer();
    return 0;
}