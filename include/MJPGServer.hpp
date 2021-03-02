// The MJPGServer code for a dual source motion JPEG server
// Right now, the code if linux only since it's using V4L2 for managing the webcam source
// The license is GPLv3
#pragma once

// We need HTTP rest server here
#include "Network/Servers/URLRouting.hpp"

#ifndef _LINUX
  // Sorry... you know what to do next ;-)
  #error "This only builds on linux. Please install a linux based distribution and retry in 15s..."
#endif

// We need V4L2 code here 
#include "V4L2Source.hpp"


// Let's inject the string class we are using
typedef Strings::FastString String;

struct MJPGServer : public V4L2Thread::PictureSink
{
    typedef Network::Socket::BaseSocket Socket;

    struct ClientSocket
    {
        Socket * clientSocket;
        bool     throttle;

        bool pictureReceived(const uint8 * data, const size_t length) {
            if (!clientSocket) return false;
            // If the socket is not able to keep up with the bandwidth, just skip the picture.
            if (throttle) {
                // Is the socket ready to receive more data ?
                throttle = !clientSocket->select(false, true, 0);
            }
            if (throttle) return true;

            // Need to send a multipart boundary here
            String boundary = String::Print("\r\n--boundary\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", length);
            
            int sent = clientSocket->send((const char*)boundary, boundary.getLength(), 0);
            if (sent != boundary.getLength()) { 
                if (sent <= 0) { 
                    // Client disconnected
                    delete0(clientSocket);
                    return false;
                }
                throttle = true; 
                // Retry, while waiting here
                sent = clientSocket->sendReliably((const char*)boundary + sent, boundary.getLength() - sent);
                if (sent <= 0) {
                    // Client disconnected or can't accept more data, let's drop it
                    delete0(clientSocket);
                    return false;
                }
            }

            // Then send the picture itself
            sent = clientSocket->sendReliably((const char*)data, length);
            if (sent != length) {
                // Bad luck here, the socket is struck
                if (sent >= 0) throttle = true; // Timeout, let's skip the next picture again
                else {
                    // Client disconnected or can't accept more data, let's drop it
                    delete0(clientSocket);
                    return false;
                }
            }
            return true;
        }

        ClientSocket(Socket * socket) : clientSocket(socket), throttle(false) {}
        ~ClientSocket() { delete0(clientSocket); }
    };

    V4L2Thread v4l2Thread;

    // The lock protecting the client list
    Threading::FastLock         lock;
    // The list of clients to send JPEG stream to
    Container::NotConstructible<ClientSocket>::IndexList clients;


//    NotConstructible::IndexList<
    Stream::InputStream * App(Network::Server::URLRouting::Comm & comm)
    {
        if (comm.method != "GET") return 0;
        String baseURL = routing.getBaseURL();

        comm.returnText = String::Print("<!doctype html>"
        "<html><body>"
        "<h1>MJPEG Streamer</h1>"
        "<div>URL list for this server:</div>"
        "<ul><li><strong>Small resolution mjpeg stream: </strong>%s/mjpg</li>"
        "<li><strong>Full resolution picture: </strong>%s/full_res</li></ul>"
        "<h2>Demo below</h2>"
        "<div><img src='/mjpg'></div>"
        "<div><button id='capt'>Full resolution</button></div>"
        "<div><img id='fr'></div>"
        "<script>var button = document.querySelector('#capt'), pic = document.querySelector('#fr');"
        "button.addEventListener('click', function(e) { e.preventDefault(); pic.src = '/full_res?time='+(new Date()).getTime(); });</script>"
        "</body></html>", (const char*)baseURL, (const char*)baseURL);

        return 0;
    }



    Stream::InputStream * FullResJPEG(Network::Server::URLRouting::Comm & comm)
    {
        if (comm.method != "GET") return 0;

        // Fetch full resolution image here
        Utils::MemoryBlock pic;
        String ret = v4l2Thread.captureFullResPicture(pic);
        if (ret) return comm.sendError(ret, Protocol::HTTP::InternalServerError);

        comm.addAnswerHeader("Content-Type", "image/jpeg");
        uint64 bufSize = pic.getSize();
        return new Stream::MemoryBlockStream(pic.Forget(), bufSize, true); 
    }

    Stream::InputStream * MotionJPEG(Network::Server::URLRouting::Comm & comm)
    {
        // Capture the socket to return the MJPEG stream
        Socket * clientSocket = const_cast<Socket*>(comm.context.client);
        if (!clientSocket) return comm.sendError("Bad state", Protocol::HTTP::InternalServerError);

        // Need to prepare the multipart stream first before going further
        String firstData = "HTTP/1.0 200 OK\r\nCache-Control: no-cache\r\nCache-Control: private\r\nContent-Type: multipart/x-mixed-replace;boundary=--boundary\r\n";
        int sent = clientSocket->sendReliably(firstData, firstData.getLength());
        if (sent != firstData.getLength()) return comm.sendError("Can't write", Protocol::HTTP::InternalServerError);

        Threading::ScopedLock scope(lock);
        size_t clientSize = clients.getSize();
        clients.Append(new ClientSocket(clientSocket));
        // If no client previously, let's create the thread to handle them
        if (!clientSize) v4l2Thread.createThread();
        comm.statusCode = Protocol::HTTP::CapturedSocket; 
        return 0;
    }

    typedef Network::Server::URLRouting URLRouting;
    URLRouting routing;


    String startServer(uint16 port)
    {
        if (!routing.registerRoute("full_res",  MakeDel(URLRouting::URLTrigger, MJPGServer, FullResJPEG, *this))) return "Can't register route: full_res";
        if (!routing.registerRoute("mjpg",      MakeDel(URLRouting::URLTrigger, MJPGServer, MotionJPEG, *this))) return "Can't register route: mjpg";
        routing.registerDefaultRoute(MakeDel(URLRouting::URLTrigger, MJPGServer, App, *this));

        if (!routing.startServer(port)) return String::Print("Failed to start server on port: %u", port);
        fprintf(stdout, "Server started on: %s\n", (const char*)routing.getBaseURL());
        return "";
    }
    bool loop() { return routing.loop(); }

    bool stopServer() { return routing.stopServer(); }


    String startV4L2Device(const char * path, int preferredVideoWidth = 640, int preferredVideoHeight = 480) { return v4l2Thread.startV4L2Device(path, preferredVideoWidth, preferredVideoHeight); }

    // PictureSink interface
private:
    bool pictureReceived(const uint8 * data, const size_t len) 
    {   // Called by the V4L2 thread, so protect it now
        Threading::ScopedLock scope(lock);
        for (size_t i = clients.getSize(); i != 0; i--) {
            if (!clients.getElementAtUncheckedPosition(i - 1)->pictureReceived(data, len))
                clients.Remove(i - 1); // Iterating from last to first allows to remove cleanly
        }
        return clients.getSize() > 0;
    }

    // Construction and destruction
public:
    MJPGServer() : v4l2Thread(*this) {}
};
        
        
