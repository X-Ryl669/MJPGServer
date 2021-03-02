#ifndef hpp_URLRouting_hpp
#define hpp_URLRouting_hpp

// We need Event HTTP server
#include "HTTP.hpp"
// We need Delegates too
#include "../../Signal/Delegate.hpp"
// We need Ternary search tree too
#include "../../Tree/TS.hpp"

namespace Network
{
    namespace Server
    {
        /** The URL routing class is used to map an URL to an action.
            URL can have captured field that are passed back to the delegate.
            This can be used for implementing a REST-like service, or for a very short-to-write HTTP server.

            Unlike other routing class with captures, we are not using regular expressions for routing,
            because it can only be implemented in O(N) search time (based on the number of possible routes).

            We are using a Ternary Search Tree for route matching so it's a O(log N) behaviour.
            The downside is that capture type can only be number or plain text (since you can parse the plain text yourself,
            it should not be a real problem for you).

            Because of uniqueness of behaviour, we are using Signal::Delegate for route action and not Signal::Source
            (a single action is expected per route).

            Other than that, this allows for implementing a complete REST server with only as many functions as there are routes.

            If you are interested in implementing a RESTful service, then this guide might help you:
            http://www.restapitutorial.com/lessons/restquicktips.html

            @param Policy   By default, URLRouting is using Comparator::ReservedComparable that uses '#' for capturing numbers, and '"' for capturing text. */
        template <class Policy>
        class URLRoutingT
        {
            // Type definition and enumeration
        public:
            /** The HeaderMap we are using (it's a hash table of an array of Strings) */
            typedef EventHTTP::HeaderMap HeaderMap;
            /** The context we are using */
            typedef TextualHeadersServer::Context Context;

            struct Comm;
            /** The routing delegate */
            typedef Signal::Delegate<Stream::InputStream * (Comm &)> URLTrigger;
            /** The search tree for the routing table */
            typedef Tree::TernarySearch::Tree<URLTrigger, char, Policy> RoutingTable;


            /** The internal server we are using */
            struct HTTPServer : public EventHTTP::Callback
            {
                virtual Stream::InputStream * clientRequested(const String & method, const String & url, HeaderMap & headers, Stream::InputStream * inputStream, Network::Address::BaseAddress & client) { return 0; }
                virtual Stream::InputStream * clientRequestedEx(const String & method, const String & url, HeaderMap & headers, Stream::InputStream * inputStream, Network::Address::BaseAddress & client, Context & context, Protocol::HTTP::StatusCode & statusCode)
                {
                    Strings::StringArray capts;
                    uint32 maxCapture = (uint32)maxCaptureCount;

                    uint32 * captArray = (uint32*)captures.getBuffer();
                    URLTrigger * trigger = table.searchForWithCapture((const char*)url, captArray, &maxCapture, url.getLength());
                    if (!trigger) trigger = defaultHandler;
                    if (!trigger) { statusCode = Protocol::HTTP::NotFound; return new Stream::InputStringStream(notFound); }

                    // Convert captures positions to captured text if any
                    for (int i = 0; i < maxCapture; i++)
                        capts.Append(url.midString(captArray[i*2+0], captArray[i*2 + 1] - captArray[i*2+0]));

                    // Call the delegate now
                    Comm comm(method, url, headers, inputStream, client, context, statusCode, capts, *this);
                    Stream::InputStream * res = (*trigger)(comm);
                    if (!res && comm.returnText) return new Stream::InputStringStream(comm.returnText);
                    // Intercept not found value to all look consistent
                    if (!res && statusCode == Protocol::HTTP::NotFound) return new Stream::InputStringStream(notFound);
                    return res;
                }
                friend struct Comm;
                /** The routing table */
                RoutingTable table;
                /** The maximum number of captures (if known beforehand), else will be auto detected */
                int maxCaptureCount;
                /** The container of captures positions */
                Utils::MemoryBlock captures;
                /** The default text to return when a resource is not found */
                String notFound;
                /** A default trigger if none provided */
                Utils::ScopePtr<URLTrigger> defaultHandler;

                HTTPServer() : maxCaptureCount(0), notFound("The requested document is not found") {}
            };

            /** The argument that the Delegate takes as input/output.
                This basically stores everything that the EventHTTP server is using in its callback */
            struct Comm
            {
                /** The requested method. This is only there for convenience, since routes are matched only against a specific method */
                const String & method;
                /** The requested URL. This is only there for convenience, you might prefer directly use the captures member for the captured elements */
                const String & url;
                /** On input, contains the client' given headers.
                    Query variables are already parsed and accessible through this hash table.
                    Pseudo query variables are also added:
                    ##METHOD##    Same as method
                    ##REQUEST##   The complete request line
                    ##VERSION##   The HTTP version requested
                    ##RESOURCE##  Same as url */
                HeaderMap & headers;
                /** On input, this contains the input stream. You should not delete this stream. */
                Stream::InputStream * inputStream;
                /** The client's address */
                Network::Address::BaseAddress & client;
                /** The context to add headers to for answering */
                Context & context;
                /** If you need to set up the returned status code */
                Protocol::HTTP::StatusCode & statusCode;
                /** For convenience, if you need to return a string to the request, use this member. If empty, the returned stream is used */
                String returnText;
                /** The captured fields (if any) */
                const Strings::StringArray & captures;
                /** A reference on the EventHTTP callback */
                HTTPServer & server;

                /** Get the complete request out of the given input stream and headers.
                    Since some client might want the direct socket stream as input, the server hasn't likely fetched all the request at when it calls you back.
                    Some request are infinite (or likely act like so), think of M/JPEG for example (continuous MIME splitted messages containing binary JPEG images)
                    Similarly, there are multiple way to express the message length. One method is to use Content-Length, and the other is to use a MIME boundary.
                    This method tries to handle the both cases above so it might never ends for continuous streams. Call it at your own risk.
                    @param timeout    The maximum time to fetch the complete request, in milliseconds.
                    @return A pointer to a new allocated stream that's contains the complete request, or 0 on timeout or socket error */
                Stream::InputStream * getCompleteRequest(const Time::TimeOut & timeout = DefaultTimeOut) { return server.getCompleteRequest(headers, inputStream, timeout); }
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
                Stream::InputStream * makeReply(const String & error, const bool logError, Protocol::HTTP::StatusCode * code = 0, Stream::InputStream * stream = 0) { return server.makeReply(error, logError, code, stream); }
                /** Create a error code with some text describing the error.
                    This method is much easier to use for only processing error.
                    @param error      The error text to include
                    @param code       The error code to set
                    @param logError   If set, also log the error using the current Logger (the mask used is Error | Network )
                    @return 0

                    Example usage:
                    @code
                        // Standard error (it's a upper layer error)
                        if (url.Find("expected") == -1) return server.sendError("The expectation is not here", Protocol::HTTP::BadArgument);
                        // Not found ?
                        if (url != "good") return server.sendError("Not found", Protocol::HTTP::NotFound);
                    @endcode */
                Stream::InputStream * sendError(const String & error, Protocol::HTTP::StatusCode code, const bool logError = true) { statusCode = code;	returnText = error; if (logError) Logger::log(Logger::Network | Logger::Connection, error); return 0; }
                /** Add a header to the answer */
                inline bool addAnswerHeader(const String & header, const String & value) { return server.addAnswerHeader(context, header, value); }

                /** Construct the Comm object */
                Comm(const String & method, const String & url, HeaderMap & headers, Stream::InputStream * inputStream, Network::Address::BaseAddress & client,
                     Context & context, Protocol::HTTP::StatusCode & statusCode, const Strings::StringArray & captures, HTTPServer & server)
                   : method(method), url(url), headers(headers), inputStream(inputStream), client(client), context(context), statusCode(statusCode), captures(captures), server(server)
                {}
            };




            // Members
        private:
            /** The server socket */
            Utils::ScopePtr<Network::Socket::BerkeleySocket> socket;
            /** The internal HTTP server is using monothreaded policy by default */
            Utils::ScopePtr<MonothreadedPolicy<EventHTTP> >  server;
            /** The actual HTTP responder */
            HTTPServer                      httpCB;

            // Interface
        public:
            /** Register a route to match.
                @param route    The route to match against. If using default policy, '#' captures numbers, and '"' captures text until next '/'
                @param action   The delegate's action
                @return true on successful adding, false if the route already exists or not enough memory */
            bool registerRoute(const String & route, const URLTrigger & action)
            {
                // Check for special capture char
                int countCapture = route.Count("#") + route.Count("\"");
                if (countCapture > httpCB.maxCaptureCount)
                {   // Reallocate the captures's buffer
                    httpCB.captures.ensureSize(countCapture * 2 * sizeof(uint32));
                    httpCB.maxCaptureCount = countCapture;
                }
                return httpCB.table.insertInTree((const char*)route, new URLTrigger(action), route.getLength());
            }
            /** Register the default route to use when none match.
                @param action   The delegate's action */
            void registerDefaultRoute(const URLTrigger & action)
            {
                httpCB.defaultHandler = new URLTrigger(action);
            }

            /** List all routes (used for debugging) */
            String listAllRoutes() const
            {
                String ret = String::Print("%d routes registered:\n", (int)httpCB.table.getSize());
                typename RoutingTable::EndIterT iter = httpCB.table.getFirstIterator();
                while (iter.isValid())
                {
                    String key((void *)0, (int)iter.computeKeyLength());
                    iter.getKeyName((char*)(const char*)key, key.getLength());

                    ret += String::Print("Route:   %s   => %s\n", (const char*)key, (*iter)->getLinkedTo() ? (*iter)->getLinkedTo() : "<unknown function>");
                    ++iter;
                }
                return ret;
            }
            /** Unregister all routes (you can not remove a single route from the table) */
            void unregisterAllRoutes()                  { httpCB.maxCaptureCount = 0; httpCB.table.Free(); }
            /** Set the not found message to use */
            void setNotFound(const String & notFound)   { httpCB.notFound = notFound; }
            /** Get the base for the URL */
            String getBaseURL() const                   { return String::Print("http://%s:%hu", (const char*)::Network::Address::IPV4::getLocalInterfaceAddress(1).asText(), Utils::ScopePtr<::Network::Address::BaseAddress>(socket->getBoundAddress())->getPort()); }

            /** Check if the server is started */
            bool isServerStarted() const { return server != 0; }
            /** Start HTTP server on the given port */
            bool startServer(const uint16 port = 80)
            {
                socket = new Network::Socket::BerkeleySocket(Network::Socket::BerkeleySocket::Stream);
                socket->setOption(Network::Socket::BaseSocket::ReuseAddress, 1);

                // Tell the server to listen on all address and port 1081
                if (socket->bindOnAllInterfaces(port) != Network::Socket::BaseSocket::Success) return false;
                server = new MonothreadedPolicy<EventHTTP>(socket->appendToMonitoringPool(0), new Network::Socket::FastBerkeleyPool(true), httpCB);

                return server->startServer();
            }
            /** Run a single loop of this server */
            bool loop(const Time::TimeOut & timeout = DefaultTimeOut) { if (!server) return false; return server->serverLoop(timeout); }
            /** Stop the HTTP server */
            bool stopServer() { if (!server) return false; return server->stopServer(); }

            // Ensure destruction order is good
            ~URLRoutingT() { stopServer(); server = 0; }
        };

        /** @copydoc URLRoutingT
            When registering a route:
             - the special character '#' means capture any number (number accepts '-0123456789.' digits),
             - the special character '"' means capture any text until end of string or '/' is found (whatever comes first) */
        typedef URLRoutingT< Tree::TernarySearch::ReservedComparable<char> > URLRouting;
    }
}

#endif
