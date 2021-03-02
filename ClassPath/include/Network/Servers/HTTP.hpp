#ifndef hpp_CPP_HTTPServer_CPP_hpp
#define hpp_CPP_HTTPServer_CPP_hpp

// We need the base server declaration
#include "../Server.hpp"
// We need protocols too
#include "../../Protocol/HTTP/HTTPCode.hpp"
// We need streams too
#include "../../Streams/Streams.hpp"

/** Network specific code, like socket classes declaration and others */
namespace Network
{
    /** All template servers are defined here */
    namespace Server
    {
        /** A powerful HTTP/1.0 server.
            This server was used to test the server code, and tweak performance so it outweight Apache's web server (by at least 40%, on a 8 core server).
            This server doesn't track clients, nor session by itself (but provides all the hooks, if you want to do so). 
            You probably want to derive from this class to provide your specific operations. 
            @sa EventHTTP for an event based HTTP server 
            
            Example usage code:
            @code
            // Create a TCP socket 
            Network::Socket::BaseSocket * socket = new Network::Socket::BerkeleySocket(Network::Socket::BerkeleySocket::Stream);
            if (!socket) return false; 
            socket->setOption(Network::Socket::BaseSocket::ReuseAddress, 1);
        
            // Tell the server to listen on all address and port 1081 
            if (socket->bind(Network::Address::IPV4((uint32)-1, 1081)) != Network::Socket::BaseSocket::Success) return false; 
            // Ok, then create a thread pool server 
            Network::Server::ThreadPoolPolicy<Network::Server::HTTP> server(socket->appendToMonitoringPool(0));
            if (!server.startServer()) return false;

            // Start the server        
            while (server.serverLoop());
            delete socket;
            @endcode */
        struct HTTP : public TextualHeadersServer
        {
            // Members
        private:
            /** The server running state */
            bool running;

            // Interface
        public:
            /** Parse a client request.
                You must read all available incoming data here, without blocking 
                If you must block, you can return NotEnoughData */
            virtual ParsingError parseRequest(InternalObject & intern, const BaseSocket & client);
            /** Handle the request itself.
                @return false on error, it will immediately close the connection */
            virtual bool handleRequest(InternalObject & intern, const BaseSocket & client);
            /** Create the main response header */
            bool createAnswerHeader(Context & context, const Protocol::HTTP::StatusCode code);
            /** Check if the method is allowed */
            virtual bool isMethodSupported(const String & method) const;

            // The interface you must provide
        public:
            /** The isRunning method */
            bool isRunning() const { return running; }
            /** Minimum amount of bytes to read before triggering the clientReadPossible callback. */
            uint32 minimumAmountToRead() const { return 16; }
            /** Maximum lingering time (in second). */
            uint32 maxLingerTime() const { return 10; }
            /** Convert a requested resource to real path on server */
            virtual String getResource(const String & requestedResource);

        public:
            /** Create an HTTP server monitoring the sockets in the given pool */
            HTTP(Network::Socket::MonitoringPool * serverPool) : TextualHeadersServer(serverPool), running(true) {}
        };
        
        /** An event based HTTP server.
            If you intend to write a very simple HTTP server, were you don't need any bells and whistle, but 
            the very basic core, you probably want to use this version. 
            This class will manage almost everything by itself and will process its work by first calling your callback.
            You don't need to handle clients (although you'll get a unique and opaque client identifier).
            @sa Callback::clientRequested */
        struct EventHTTP : public HTTP
        {
            // Type definition and enumeration
        public:
            /** The callbacks you must implement */
            struct Callback
            {
                /** Some client asked for a specific resource
                    @param method       The textual version of the method
                    @param url          The URL of the queried resource  
                    @param headers      The headers sent by the client.
                                        Query variables are already parsed and accessible through this hash table.
                                        Pseudo query variables are also added: 
                                        ##METHOD##    Same as method
                                        ##REQUEST##   The complete request line
                                        ##VERSION##   The HTTP version requested
                                        ##RESOURCE##  Same as url
                    @param inputStream  If provided by the client, will point to a stream you might have to read to actually understand the request (else 0). 
                                        This stream is not buffered and you must not delete this stream.
                    @param client       The client address.
                    @return 0 to close the connection with a 404 Not Found error, or a 
                            pointer on a new allocated InputStream to send as the content (you can use addAnswerHeader() if you need to change the headers) */
                virtual Stream::InputStream * clientRequested(const String & method, const String & url, HeaderMap & headers, Stream::InputStream * inputStream, Network::Address::BaseAddress & client) = 0;
                /** Some client asked for a specific resource.
                    This call the simple version by default. You can overload this one only if you need more features.
                    @warning The inputStream that's given in this version is based on a BaseSocketStream, so the returned size is the stream size at the time of the request.
                             More data are likely to come, so you must find the amount data to query by yourself (usually based on the Content-Length header or by some
                             MIME's boundary), and you should read the stream for this amount. You can use the getCompleteRequest() helper method to let it do that for you.

                    @param method       The textual version of the method
                    @param url          The URL of the queried resource
                    @param headers      The headers sent by the client
                    @param inputStream  If provided by the client, will point to a stream you might have to read to actually understand the request (else 0).
                                        This stream is not buffered and you must not delete this stream.
                    @param client       The client address.
                    @param context      If you need to set up answer HTTP header, you'll need this context.
                    @param statusCode   On output, it can be set to any HTTP compatible status code
                    @return 0 to close the connection (you need to set the statusCode to something like 404), or a
                            pointer on a new allocated InputStream to send as the content (you can use addAnswerHeader() if you need to change the headers) */
                virtual Stream::InputStream * clientRequestedEx(const String & method, const String & url, HeaderMap & headers, Stream::InputStream * inputStream, Network::Address::BaseAddress & client, Context & context, Protocol::HTTP::StatusCode & statusCode)
                {
                    Stream::InputStream * completeStream = getCompleteRequest(headers, inputStream, Time::TimeOut(DefaultTimeOut, true)); // Glob the request
                    Stream::InputStream * stream = clientRequested(method, url, headers, completeStream, client);
                    delete completeStream;
                    statusCode = stream ? Protocol::HTTP::Ok : Protocol::HTTP::NotFound;
                    return stream;
                }
                /** Requested destructor */
                virtual ~Callback() {}

                // Helper method
            protected:
                /** Get the complete request out of the given input stream and headers.
                    Since some client might want the direct socket stream as input, the server hasn't likely fetched all the request at when it calls you back.
                    Some request are infinite (or likely act like so), think of M/JPEG for example (continuous MIME splitted messages containing binary JPEG images)
                    Similarly, there are multiple way to express the message length. One method is to use Content-Length, and the other is to use a MIME boundary.
                    This method tries to handle the both cases above so it might never ends for continuous streams. Call it at your own risk.
                    @param headers    The request headers
                    @param inStream   The input stream
                    @param timeout    The maximum time to fetch the complete request, in milliseconds.
                    @return A pointer to a new allocated stream that's contains the complete request, or 0 on timeout or socket error */
                Stream::InputStream * getCompleteRequest(HeaderMap & headers, Stream::InputStream * inputStream, const Time::TimeOut & timeout);
                /** Create a error code with some text describing the error (if the default does not fit).
                    This is just a convenient helper to make the usage code smaller and less error prone.
                    @param error      The error text to include
                    @param logError   If set, also log the error using the current Logger (the mask used is Error | Network )
                    @param stream     If provided then it's returned, and "error" is only used for logging.
                    @param code       The error code to set (if provided, it's set based on the "error" or stream parameter:
                                      if no stream is provided, it's set to NotFound, else it's set to "Ok")
                    @return A pointer on a new allocated stream you can return from clientRequestedEx method
                    
                    Example usage:
                    @code
                    // Standard error (HTTP code will be OK, it's a upper layer error)
                    if (url.Find("expected") == -1) return server.makeReply("The expectation is not here", true);
                    // Not found ?
                    if (url != "good") return server.makeReply("Not found", true, &statusCode);
                    // Standard usage
                    if (url == "good") return server.makeReply("", false, &statusCode, new InputStringStream("The content you expected"));
                    @endcode */
                Stream::InputStream * makeReply(const String & error, const bool logError, Protocol::HTTP::StatusCode * code = 0, Stream::InputStream * stream = 0);
                /** Add a header to the answer */
                inline bool addAnswerHeader(Context & context, const String & header, const String & value) { return HTTP::addAnswerHeader(context, header, value); }
            };
            
            // Members
        private:
            /** The callback class to use */
            Callback & callback;
            friend struct Callback;

            // Helpers
        protected:
            /** Check if the method is allowed */
            inline bool isMethodSupported(const String &) const { return true; } // No limit
            /** Handle the request itself.
                @return false on error, it will immediately close the connection */
            virtual bool handleRequest(InternalObject & intern, const BaseSocket & client);
            /** Implement the socket option changing  */
            virtual void changeSocketOptions(BaseSocket & client, const bool accepting) { if (accepting) client.setOption(BaseSocket::NoDelay, 1); }
            
            // Interface
        public:
            /** The unique constructor 
                @param callback     The callback to use when receiving requests
                @param serverPool   The pool of sockets to monitor */
            EventHTTP(Callback & callback, Network::Socket::MonitoringPool * serverPool) : HTTP(serverPool), callback(callback) {}
        };
    }
}

#endif
