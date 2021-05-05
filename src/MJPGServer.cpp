/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */

// We need our own include
#include "../include/MJPGServer.hpp"
#include "../include/JSON.hpp"


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
Configuration config;

String Configuration::fromJSON(const String & path) 
{
    File::Info cfg(path, true);
    if (!cfg.doesExist()) return "Configuration file not found";
    String content = cfg.getContent();

    // Then provide a parser for the JSON data now
    const char * buffer = content;
    JSON parser;
    JSON::Token tokens[2000];    // Should be enough
    IndexType res = parser.parse(buffer, content.getLength(), tokens, ArrSz(tokens));
         if (res == JSON::NotEnoughTokens) return "Not enough tokens";
    else if (res == JSON::Invalid)         return String::Print("Invalid configuration JSON at pos: %d\n", parser.pos);
    else if (res == JSON::Starving)        return String::Print("Configuration JSON too short at pos: %d\n", parser.pos);

    // Then extract the token we are interested in
    for (IndexType i = 0; i < res; i++) 
    {
        JSON::Token & t = tokens[i];
        if (t.type == JSON::Token::Key && i < res-1 && (tokens[i+1].type == JSON::Token::String || tokens[i+1].type == JSON::Token::Number || tokens[i+1].type == JSON::Token::True || tokens[i+1].type == JSON::Token::False))
        {
            JSON::Token & n = tokens[i+1];
            String key = content.midString(t.start, t.end - t.start), val = content.midString(n.start, n.end - n.start);
            if (key == "port")                       port = (unsigned int)val; 
            else if (key == "device")                device = n.unescape((char*)(const char*)content); 
            else if (key == "daemonize")             daemonize = n.type == JSON::Token::True; 
            else if (key == "logLevel")              logLevel = (unsigned int)val; 
            else if (key == "lowResWidth")           lowResWidth = (unsigned int)val; 
            else if (key == "lowResHeight")          lowResHeight = (unsigned int)val; 
            else if (key == "closeDeviceTimeoutSec") closeDevTimeoutSec = (unsigned int)val; 
            else if (key == "securityToken")         securityToken = n.unescape((char*)(const char*)content); 
            else log(Warning, "Ignoring unsupported key: %s", (const char*)key);

            i++;
        }
    }
    return "";
}

int main(int argc, const char ** argv)
{
    signal(SIGINT, asyncProcess);

    String cfgFile;

    // Options
    Arguments::declare(config.daemonize,          "Daemonize the server", "daemon", "d");
    Arguments::declare(config.port,               "The port to listen on", "port", "p");
    Arguments::declare(config.device,             "Path to the device to use", "camera", "c");
    Arguments::declare(logLevel,                  "Log level (-1: debug, 0: Info, 2: Error, 3: Silent)", "loglevel", "v");
    Arguments::declare(config.lowResWidth,        "The low resolution picture width (default 640px)", "width", "w");
    Arguments::declare(config.lowResHeight,       "The low resolution picture height (default 480px)", "height", "t");
    Arguments::declare(cfgFile,                   "Configuration file path (default none)", "cfg", "j"); 
   

    fprintf(stdout, "Thank you for using MJPGServer\n");

    String error = Arguments::Core::parse(argc, argv);
    if (error) return log(Error, "%s", (const char*)error);

    if (cfgFile) {
        error = config.fromJSON(cfgFile);
        if (error) return log(Error, "%s", (const char*)error); 
    }

    if (config.daemonize) {
        bool parent = false;
        if (!Platform::daemonize("/var/run/mjpgsrv.pid", "mjpgsrv", parent))
            return log(Error, "Can't daemonize");
        if (parent) _exit(0);

        // Now we are the child daemon, we'll need to drop priviledges ASAP
    }


    MJPGServer srv;
    error = srv.startV4L2Device();
    if (error) return log(Error, "%s\n", (const char*)error);

    error = srv.startServer();
    if (error) return log(Error, "%s\n", (const char*)error);

    Platform::dropPrivileges(); // We don't need any priviledge anymore here, since we have opened the server socket and camera device already
    while (!exitRequired && srv.loop()) {}

    srv.stopServer();
    return 0;
}