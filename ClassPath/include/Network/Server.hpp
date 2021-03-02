#ifndef hpp_CPP_Server_CPP_hpp
#define hpp_CPP_Server_CPP_hpp

// We need address declarations
#include "Connection.hpp"
// We need thread declaration too
#include "../Threading/Threads.hpp"
// We need hash
#include "../Hash/HashTable.hpp"
// We need files too
#include "../File/File.hpp"
// We need the logger
#include "../Logger/Logger.hpp"
// We need utility classes too
#include "../Utils/ScopePtr.hpp"
// We need random function too
#include "../Crypto/Random.hpp"
// We need memory blocks too
#include "../Utils/MemoryBlock.hpp"
// We need input file stream too
#include "../Streams/Streams.hpp"
// We need socket stream too
#include "../Streams/SocketStream.hpp"

/** Network specific code, like socket classes declaration and others */
namespace Network
{
    /** All template servers are defined here */
    namespace Server
    {
        /** The socket class we are using */
        typedef Network::Socket::BaseSocket BaseSocket;
        /** The address class we are using */
        typedef Network::Address::BaseAddress BaseAddress;
        /** The connection */
        typedef Network::Connection::BaseConnection BaseConnection;


        /** The socket private buffer in a server will points to this structure.
            Don't change the socket private buffer directly, call the method from
            the object below. You have private field for this. */
        struct InternalObject
        {
            // Members
        protected:
            /** Current receive buffer. */
            Utils::MemoryBlock      recvBuffer;
            /** Current send buffer */
            Utils::MemoryBlock      prefixBuffer;
            /** The file to send (if any) */
            Stream::InputStream *   streamToSend;
            /** Current send buffer */
            Utils::MemoryBlock      suffixBuffer;
            /** Should the socket be closed after sending */
            bool                    closeAfterSend;
            /** The connection first timestamp (in second, used to lingering connections) */
            int                     firstTime;
            /** The private data (if any) */
            void *                  priv;
            /** Flag to reset the memory but not close the connection : the reset set this flag to false */
            bool                    resetAfterSend;
            /** Flag to indicate that the client as asked the connection to close upon reply */
            bool                    clientAskedConnectionClose;
            /** Flag to indicate that the client want to take other the socket */
            bool                    forgetSocket;
            /** Cork the connection */
            bool                    corkSocket;
            /** (cache) Contains the sum in bytes for all data sent */
            uint64                  sentSize;
            /** (cache) Contains the sum in bytes for all data to send */
            uint64                  fullSize;
            /** (cache) The TCP MSS size */
            int                     tcpMSS;

            // Interface
        public:
            /** Reset the structure but don't delete the private data */
            void Reset();
            /** Call this to reset the lingering connection timestamp (and avoid being auto-closed) */
            void touchConnection();
            /** Get a reference on the private data.
                As for socket, the object doesn't clean the priv data you've set on destruction */
            inline void * & getPrivateField() { return priv; }
            /** Check if socket should be cleaned by timeout */
            bool shouldBeCleaned(const int maxTimeoutInSec) const;
            /** Get the memory block of received data so far */
            inline Utils::MemoryBlock & getRecvBuffer() { return recvBuffer; }
            /** Get the memory block of sent data so far */
            inline Utils::MemoryBlock & getPrefixBuffer() { return prefixBuffer; }
            /** Get the memory block of sent data so far */
            inline Utils::MemoryBlock & getSuffixBuffer() { return suffixBuffer; }
            /** Set the stream to send.
                The stream is send in-place of the file (you can't have both)
                @param inStream     A pointer on a new allocated stream that's owned.
                @return false in case it's not possible to set this stream to send */
            virtual bool setStreamToSend(::Stream::InputStream * inStream);
            /** Set the file to send */
            virtual bool setFileToSend(const String & fullPath);
            /** Set the file to send */
            virtual bool setFileToSend(const File::Info & info);
            /** Get the stream to send (don't delete the returned pointer) */
            inline Stream::InputStream * getStreamToSend()     { return streamToSend; }

            /** Close the socket after sending */
            inline void closeAfterSending() { closeAfterSend = true; }
            /** Check if the socket should close after sending */
            inline bool shouldCloseAfterSending() const { return closeAfterSend; }
            /** Reset the object after sending */
            inline void resetAfterSending() { resetAfterSend = true; }
            /** Check if the object should be reset after sending */
            inline bool shouldResetAfterSending() const { return resetAfterSend; }
            /** Check if we have data to send */
            bool hasDataToSend() const;
            /** Client has asked that the connection must be closed upon reply */
            inline bool shouldCloseConnectionOnReply() const  { return clientAskedConnectionClose; }
            /** Force the connection to close upon reply */
            inline void closeConnectionOnReply(const bool shouldClose) { clientAskedConnectionClose = shouldClose; }
            /** Cork the connection */
            inline void corkConnection() { corkSocket = true; }
            /** Shoud we cork the connection ? */
            inline bool shouldCork() const { return corkSocket; }

            /** Should we forget the socket */
            inline bool shouldForgetSocket() const { return forgetSocket; }
            /** Capture the socket */
            inline void captureSocket() { forgetSocket = true; }
            /** Compute the full size of data to send */
            inline void computeFullSize() { fullSize = prefixBuffer.getSize() + suffixBuffer.getSize() + (streamToSend ? streamToSend->fullSize() : 0); sentSize = 0; }
            /** Get the full size (only if computed) */
            inline uint64 getFullSize() const { return fullSize; }
            /** Track the sending progress */
            inline void sentBytes(const uint64 sent) { sentSize += sent; }
            /** Set the cork limit (after fullSize - limit, the cork option is removed) */
            inline void setCorkLimit(int limit) { tcpMSS = corkSocket ? limit : 0; if (limit == 0) corkSocket = false; }
            /** Get the amount of byte remaining before removing the cork */
            uint64 corkRemaining() const { return fullSize - sentSize - tcpMSS; }

            InternalObject();
            virtual ~InternalObject();
        };

        /** This is the common implementation of sendPendingData that's moved
            here to avoid code duplication because of different template instantiation
            @return 1 on successful and complete sending,
                    0 on failed send,
                   -1 on partial sending */
        int sendPendingDataImpl(BaseSocket * socket, InternalObject * intern);

        /** The server policy (used in the base server interface). */
        /** The monothreaded policy use a monitoring pool to select
            on non-blocking socket. The monothreaded policy prefers answering connections
            over accepting new connections.

            The code typically wait for connection, and then answer them.
            If any answer takes too long (or "would block"), then the server still wait for new
            connection before trying to answer again.


            For performance reasons, the inheritance use template instead of virtual table.
            This avoid a pointer dereference for each operation.


            The base class (template argument) must have the following signature:
            @code
            class Base
            {
            protected:
                MonitoringPool  pool; // Server pool
                // Callback : A client has been rejected (no more space in pool)
                void rejectedNewClient(BaseSocket * client, BaseAddress * address);
                // Callback : A client has been accepted
                bool acceptedNewClient(BaseSocket * client, BaseAddress * address);
                // Callback : The given socket is now readable. You must read all available data from that socket
                //            If you want to send a file or some data, you can fill the appropriate parameters, the server will send them
                //            You can't use this possibility if you want to monitor write availability
                bool clientReadPossible(InternalObject * client, BaseSocket & socket);
                // Callback : The given socket is now writable.
                bool clientWritePossible(InternalObject * client);
                // Callback : Client connection closed
                void clientConnectionClosed(BaseSocket * client, InternalObject * intern);
                // Callback for customization : Return true if you want the server to monitor when a socket is writable
                bool wantToMonitorWrite();
                // Return true while the server is running
                bool isRunning() const;
                // Minimum amount of bytes to read before triggering the clientReadPossible callback
                uint32 minimumAmountToRead() const;
                // Maximum lingering time (in second)
                uint32 maxLingerTime() const;
                // Write loop finished callback
                void writeLoopFinished();
                // This is called when it's required to delete the private connection data
                bool deletePrivateField(void * toDelete);
            };

            // If you want a monothreaded server, you'll write:

            // serverSocketPool has been created earlier, and clientPool can be "new FastBerkeleyPool(true)"
            MonothreadedPolicy<Base> server(serverSocketPool, clientPool);
            // Start the server (this puts socket in listen mode if not done already)
            if (!server.startServer()) return false;
            // Ok, the main server loop (using 3s timeout as default)
            while (server.serverLoop());
            // Need to call this to release the pending sockets (done in destructor anyway,
            // but it's usually more intuitive to have a start / stop method)
            if (!server.stopServer()) return false;

            @endcode
            @warning Usually, it's better to let the client pool "owns" its sockets.
            @warning Never, ever call a method from this class from a callback (as you'll probably crash)
        */
        template<class T>
        struct MonothreadedPolicy : public T
        {
            /** The client monitoring pool */
            Network::Socket::MonitoringPool & clients;
            /** The pool of clients that requires writing */
            Network::Socket::MonitoringPool * writers;
            /** The server socket's index in the client pool */
            uint32                            serverIndex;

            /** Check if the server is still running */
            inline bool isRunning() const { return static_cast<const T*>(this)->isRunning(); }

            /** Start the server (you must have set the port & address to bind on) */
            inline bool startServer()
            {
                serverIndex = 0;
                if (!this->pool.getSize()) return false;

                for (uint32 i = 0; i < this->pool.getSize(); i++)
                {   // Set socket as non blocking
                    BaseSocket * socket = this->pool[i];
                    if (socket)
                    {
                        // Make sure the server socket is correclty set up
                        if (socket->getState() < Network::Socket::BaseSocket::Bound) return false;
                        if (socket->getState() < Network::Socket::BaseSocket::Listening
                            && socket->getType() == Network::Socket::BaseSocket::Stream
                            && socket->listen() != Network::Socket::BaseSocket::Success) return false;

                        socket->setOption(Network::Socket::BaseSocket::Blocking, 0);

                        serverIndex++;
                        clients.appendSocket(socket);
                        this->pool.forgetSocket(socket);
                    }
                }

#ifdef _POSIX
                struct sigaction actual;
                if (sigaction(SIGPIPE, (struct sigaction *)NULL, &actual) != 0) return false;
                if (actual.sa_handler == SIG_DFL)
                {   // Ignore default action on SIGPIPE (avoid being killed)
                    actual.sa_handler = SIG_IGN;
                    if (sigaction(SIGPIPE, &actual, (struct sigaction *)NULL) != 0) return false;
                }
#endif
                return true;
            }

            /** The main server accept loop */
            inline bool serverLoop(const Time::TimeOut & timeout = DefaultTimeOut)
            {
                if (!isRunning()) return false;
                this->serverLooping();

                // Check the pool if any accepting event occured
                if (!serverIndex) return false;

                // Make a single pool from both server & clients.
                // This will be required to avoid spending the complete timeout in the server code and none
                // in the client code
                int action = clients.selectMultiple(writers, timeout);
                if (action & 1)
                {   // Reading
                    uint32 clientIndex = clients.getNextReadySocket(-1);
                    while (clientIndex != (uint32)-1)
                    {
                        BaseSocket * socket = clients.getReadyAt(clientIndex);
                        if (socket)
                        {
                            InternalObject *& intern = (InternalObject *&)socket->getPrivateField();
                            if (!intern)
                            {    // Likely a server socket, let's make sure about it
                                 if (clients.indexOf(socket) < serverIndex) // Without the test for the private field, this would do a O(N) search, but here server sockets are at first of array in in limited number, so it's OK.
                                 {   // Server socket, let's accept the socket
                                     BaseAddress * address = 0;
                                     BaseSocket * client = socket->accept(address);
                                     if (client)
                                     {   // Call the callback
                                         if (!clients.appendSocket(client))
                                         {
                                             // Can't accept this client
                                             this->rejectedNewClient(*client, *address);
                                             delete address;
                                             delete client;
                                         } else
                                         {
                                             // Ok, the client was accepted
                                             client->getPrivateField() = (void *)new InternalObject;
                                             client->setOption(Network::Socket::BaseSocket::Blocking, 0);
                                             if (!this->acceptedNewClient(*client, *address))
                                                 closeClientSocket(client);
                                         }
                                     }
                                     // Not needed anymore
                                     delete address;
                                     clientIndex = clients.getNextReadySocket(clientIndex);
                                     continue;
                                 }
                            }
                            // CLIENTS HERE -- From now on, it's a client that needs to be read
                            if (intern->shouldBeCleaned(this->maxLingerTime()))
                            {   // Bad object, let's remove it
                                this->clientLingering(*socket);
                                closeClientSocket(socket);
                                break;
                            }
                            uint32 minRecvSize = this->minimumAmountToRead();
                            // Ok, make progress in receiving
                            if (!receiveAsMuchAsPossible(socket, intern))
                                break;

                            if (intern->getRecvBuffer().getSize() > minRecvSize)
                            {   // Have enough data to tell the client ?
                                if (!this->clientReadPossible(*socket, *intern))
                                {   // Ok, close the connection
                                    closeClientSocket(socket);
                                    return false;
                                }
                                // Check if we need to forget this socket
                                if (intern->shouldForgetSocket())
                                {
                                    // That's the only case where we send data in the receiving part, since after forgetting, there is no link to the socket any more
                                    if (intern->hasDataToSend())
                                        sendPendingData(socket, intern);
                                    forgetClientSocket(socket);
                                }
                                else
                                {
                                    // Ok, the connection progressed, so let's touch it
                                    intern->touchConnection();
                                    // Check if the clients should be written too (if so, add to the writer pool)
                                    if (intern->hasDataToSend())
                                        writers->appendSocket(socket);
                                }
                            }
                        }
                        clientIndex = clients.getNextReadySocket(clientIndex);
                    } // while clientindex
                }
                if (action & 2)
                {   // Writing
                    uint32 clientIndex = writers->getNextReadySocket(-1);
                    while (clientIndex != (uint32)-1)
                    {
                        BaseSocket * socket = writers->getReadyAt(clientIndex);
                        if (socket)
                        {
                            InternalObject * intern = (InternalObject*)socket->getPrivateField();
                            int sendResult = sendPendingData(socket, intern);
                            if (!sendResult)
                            {
                                // Delete the private field
                                closeClientSocket(socket);
                                intern = 0;
                            }
                            // The sending has been done, so touch the watchdog
                            if (intern)
                            {
                                intern->touchConnection();

                                if (sendResult > 0)
                                {   // Data was completely sent, there's no point in monitoring it again
                                    writers->forgetSocket(socket);
                                    // If the sending was complete, reset the buffer
                                    if (intern->shouldResetAfterSending())
                                    {
                                        // Reset doesn't manage the private field
                                        intern->Reset();
                                        this->deletePrivateField(intern->getPrivateField());
                                    }
                                    // A client close connection might be requested
                                    if (this->wantToMonitorWrite() && !intern->hasDataToSend() && !this->clientWritePossible(*socket, *intern))
                                    {
                                        closeClientSocket(socket);
                                        intern = 0;
                                    }
                                    // Ok, the connection progressed, so let's touch it
                                    if (intern) intern->touchConnection();
                                }
                            }
                        }
                        clientIndex = writers->getNextReadySocket(clientIndex);
                    }
                    this->writeLoopFinished();
                }

                // Look through the client array to remove lingering one
                for (uint32 i = 0; i < clients.getSize(); i++)
                {
                    BaseSocket * socket = clients[i];
                    if (socket && (InternalObject*)socket->getPrivateField()
                        && ((InternalObject*)socket->getPrivateField())->shouldBeCleaned(this->maxLingerTime()))
                    {
                        this->clientLingering(*socket);
                        closeClientSocket(socket);
                    }
                }
                return true;
            }
            /** Stop the server */
            inline bool stopServer()
            {
                this->serverStopping();

                if (!serverIndex) return false;
                // Re-add server sockets
                for (uint32 i = 0; i < serverIndex; i++)
                {
                    BaseSocket * socket = clients[0];
                    if (socket)
                    {
                        clients.forgetSocket(socket);
                        this->pool.appendSocket(socket);
                        socket->setOption(Network::Socket::BaseSocket::Blocking, 1);
                    }
                }
                return true;
            }
            /** Close client socket */
            inline void closeClientSocket(Network::Socket::BaseSocket * socket)
            {
                if (!socket) return;
                InternalObject *& intern = (InternalObject*&)socket->getPrivateField();
                if (intern)
                {
                    this->clientConnectionClosed(*socket, *intern);
                    this->deletePrivateField(intern->getPrivateField());
                    delete0(intern);
                }
                writers->forgetSocket(socket);
                clients.removeSocket(socket);
            }
            /** Forget client socket */
            inline void forgetClientSocket(Network::Socket::BaseSocket * socket)
            {
                if (!socket) return;
                InternalObject *& intern = (InternalObject*&)socket->getPrivateField();
                if (intern)
                {
                    this->deletePrivateField(intern->getPrivateField());
                    delete0(intern);
                }
                writers->forgetSocket(socket);
                clients.forgetSocket(socket);
            }
            /** Receive as much as possible non blocking */
            bool receiveAsMuchAsPossible(BaseSocket * socket, InternalObject * intern)
            {
                while (true)
                {
                    char Buffer[4096];
                    int received = socket->receive(Buffer, ArrSz(Buffer), 0);
                    if (received == 0)
                    {
                        // Let the server know we have received data anyway
                        if (intern->getRecvBuffer().getSize())
                            this->clientReadPossible(*socket, *intern);

                        closeClientSocket(socket);
                        return false;
                    }
                    if (received > 0 && !intern->getRecvBuffer().Append((uint8*)Buffer, received))
                    {
                        Logger::log(Logger::Connection | Logger::Creation, "Server : reception buffer cannot be added\n");
                        closeClientSocket(socket);
                        return false;
                    }
                    if ((received > 0 && received < (int)ArrSz(Buffer)) || (received < 0 && socket->getLastError() == Network::Socket::BaseSocket::InProgress))
                        return true;
                }
                return false;
            }
            /** Ok, send the programmed data now
                We are using a non-templated version to avoid code duplication
                @return 1 on successful and complete sending,
                        0 on failed send,
                       -1 on partial sending */
            int sendPendingData(BaseSocket * socket, InternalObject * intern)
            {
                return sendPendingDataImpl(socket, intern);
            }

            /** We only need the server pool and client pool type */
            MonothreadedPolicy(Network::Socket::MonitoringPool * serverPool, Network::Socket::MonitoringPool * clientPool) : T(serverPool), clients(*clientPool), writers(clientPool->createEmpty(false)), serverIndex(0) {}
            /** Any additional server parameters can be passed to this template method
                @param serverPool   The pool of server socket to monitor
                @param clientPool   The pool of client socket to monitor (usually empty unless you're bootstrapping a running server)
                @param arg          This is an additional parameter that's passed to the Server constructor (in addition to the server pool) */
            template <class Y>
            MonothreadedPolicy(Network::Socket::MonitoringPool * serverPool, Network::Socket::MonitoringPool * clientPool, Y & arg) : T(arg, serverPool), clients(*clientPool), writers(clientPool->createEmpty(false)), serverIndex(0) {}
            /** Destruct the server. This actually deregister the server's sockets from the monitoring pool.
                @warning If you are forking (like with daemonizing) and destruct the parent's version of the server, then the child process will have the monitoring pool modified too. */
            ~MonothreadedPolicy()
            {
                stopServer();
                for (uint32 i = this->clients.getSize(); i > 0; i--)
                    closeClientSocket(this->clients[i - 1]);
                delete (Network::Socket::MonitoringPool *)&clients;
                delete writers;
            }
        };

        // The thread pool policy is too big to be included here
#if !defined(_BASIC_NEXIO_)
    #define InServerHPP
    #include "ThreadPoolPolicy.hpp"
    #undef  InServerHPP
#endif

        /** The base server interface */
        struct BaseServer
        {
        protected:
            /** The server monitoring pool (the server can listen on multiple sockets) */
            Network::Socket::MonitoringPool  & pool;

            // Callback interface
        public:
            /** A client has been rejected (no more space in pool) */
            void rejectedNewClient(const BaseSocket & client, const BaseAddress & address);
            /** A client has been accepted */
            void acceptedNewClient(const BaseSocket & client, const BaseAddress & address);
            /** The given socket is now readable (and has been read).
                If you want to send a file or some data, you can fill the appropriate parameters, the server will send them.
                @param intern           The client internal object you need to fill / act upon
                @param client           The client socket. Don't call blocking functions the socket connection here (like receive / send).
                @return If you return false the socket will be deleted immediately */
            bool clientReadPossible(const BaseSocket & client, InternalObject & intern);
            /** The given socket is now writable. */
            bool clientWritePossible(const BaseSocket & client, InternalObject & intern);
            /** The client (or server) closed its connection with the server (or client)
                If you have saved data in the private field on the internal object, it's the last time to clean them. */
            void clientConnectionClosed(const BaseSocket & client, InternalObject & intern);
            /** The client (or server) lingers on the server.
                No action is required from this callback, since the server will immediately close the socket
                when you return from this function. */
            void clientLingering(const BaseSocket & client);
            /** Minimum amount of bytes to read before triggering the clientReadPossible callback.
                This is useful for example, if your protocol is fixed size message, you don't have to deal with reconstructing the buffers yourselves.
                This is also useful for textual protocol, where parsing a line can be slow, you can 'prebuffer' a given amount
                For example, in HTTP, 'GET / HTTP/x.x\n\n' is the minimal request, so it might be interesting to set the amount to 16 bytes.
                @return 0 (default) if you want to be triggered as soon as possible. */
            uint32 minimumAmountToRead() const;
            /** Maximum lingering time (in second).
                To avoid requests flood, all servers implements a simple timeout based limit.
                If a connection doesn't progress for the returned amount of time (in second), then it's deleted.
                @return 30 (default) if you allow 30s between each progress on the connection. */
            uint32 maxLingerTime() const;

            /** The server will call this to check if you want to monitor when a socket is ready.
                You'll unlikely have to change this
                @return true if you want the server to monitor when a socket is writable */
            bool wantToMonitorWrite();
            /** Write loop finished callback
                This is called when all the socket in the pool have been written too */
            void writeLoopFinished();
            /** This is called when it's required to delete the private connection data */
            bool deletePrivateField(void * toDelete);

            /** Return true while the server is running */
            bool isRunning() const;
            /** The server is stopping (this is called before any internal closing action happened) */
            void serverStopping();
            /** The server is going to loop once (this is called before any internal loop action happened) */
            void serverLooping();

        private:
            /** Prevent instantiating this object (only used as a documentation example) */
            BaseServer();

        };

        /** A textual header server specialization.
            Those servers are typically used to receive textual based headers, followed by unspecified content.
            An HTTP server is a kind of such server, expecting CRLF headers, and then binary data (can be HTML / XML / images etc...)
            A RTSP server is also very similar to HTTP */
        struct TextualHeadersServer
            // : public BaseServer // Not required as we are using template, so it speeds up answering
        {
            // Type definition and enumeration
        public:
            /** The string we are using */
            typedef Strings::FastString String;
            /** The header map we are using */
            typedef Container::HashTable<String, String, Container::HashKey<String>, Container::DeletionWithDelete<String> > HeaderMap;

            /** The possible parsing error */
            enum ParsingError
            {
                Success         =   0,
                BadRequest      =  -1,
                NotEnoughData   =  -2,
            };

            /** The request parsing context */
            struct Context
            {
                /** The header hash map for the request */
                HeaderMap                           query;
                /** The header hash map for the answer */
                HeaderMap                           answer;
                /** The client socket (when provided, it can change) */
                const BaseSocket *                      client;

                inline void Reset() { query.clearTable(); answer.clearTable(); }
                /** Constructor
                    Limit the hash table default bucket size */
                Context() : query(33), answer(11), client(0) {}
            };

        protected:
            /** Get the current context */
            Context * getContext(InternalObject & intern) { return (Context*)intern.getPrivateField(); }
            /** Add a header to the query */
            bool addQueryHeader(Context & context, const String & header, const String & value);
            /** Add a header to the answer */
            static bool addAnswerHeader(Context & context, const String & header, const String & value);
            /** Merge all headers to data */
            bool mergeAnswerHeaders(InternalObject & intern);



            // Members
        protected:
            /** The server monitoring pool (the server can listen on multiple sockets) */
            Network::Socket::MonitoringPool &   pool;

            // Callback interface
        public:
            /** A client has been rejected (no more space in pool) */
            void rejectedNewClient(const BaseSocket &, const BaseAddress & address)
            {
                Logger::log(Logger::Connection | Logger::Error, "Refused connection from: (%s:%d) [NO FREE SLOT]", (const char*)address.asText(), address.getPort());
            }
            /** A client has been accepted */
            bool acceptedNewClient(BaseSocket & a, const BaseAddress & address)
            {
                Logger::log(Logger::Dump, "Accepted connection from: (%s:%d) [SUCCESS]", (const char*)address.asText(), address.getPort());
                changeSocketOptions(a, true);
                return true;
            }
            /** The client (or server) closed its connection with the server (or client) */
            void clientConnectionClosed(const BaseSocket & client, InternalObject & intern)
            {
                Context *& context = (Context*&)intern.getPrivateField();
                delete0(context);
                BaseAddress * address = client.getPeerName();
                if (address)
                    Logger::log(Logger::Dump, "Connection from: (%s:%d) closed [SUCCESS]", (const char*)address->asText(), address->getPort());
                delete address;

            }
            /** The client (or server) lingers on the server. */
            void clientLingering(const BaseSocket & client)
            {
                BaseAddress * address = client.getPeerName();
                if (address)
                    Logger::log(Logger::Connection | Logger::Deletion, "Connection from: (%s:%d) is inactive for too long, closing it [SUCCESS]", (const char*)address->asText(), address->getPort());
                delete address;
            }

            /** Clean our private stuff */
            bool deletePrivateField(void *& toDelete)
            {
                // we have allocated by a context
                Context * typedPointer = (Context *)toDelete;
                delete(typedPointer); toDelete = 0;
                return true;
            }

            /** The given socket is now readable.
                If you want to send a file or some data, you can fill the appropriate parameters, the server will send them.
                @param intern           The client internal object you need to fill / act upon
                @param client           The client socket. Don't call blocking functions the socket connection here (like receive / send).
                @return If you return false the socket will be deleted immediately */
            bool clientReadPossible(const BaseSocket & client, InternalObject & intern)
            {
                // Read as much data as possible from the socket
                Context * context = (Context*)intern.getPrivateField();
                if (context == 0)
                {
                    intern.getPrivateField() = (void*)new Context;
                    context = (Context*)intern.getPrivateField();
                    if (!context) return false;
                } else
                {
                    if (intern.hasDataToSend())
                        // Some data are pending to be sent so return, we'll process them later
                        return true;
                    // Else, we should reset the context, for processing the next request
                    context->Reset();
                }

                if (context->query.getValue("##RESOURCE##") == 0)
                {   // As soon as the query is received, we skip this parsing
                    context->client = &client;
                    ParsingError error = parseRequest(intern, client);
                    if (error == BadRequest)
                    {
                        BaseAddress * address = client.getPeerName();
                        if (address)
                            Logger::log(Logger::Content | Logger::Error, "Invalid request from (%s:%d) %s [BAD REQUEST]", (const char*)address->asText(), address->getPort(), context->query.getValue("##REQUEST##") ? (const char*)(*context->query.getValue("##REQUEST##")) : "Unknown request");
                        delete address;
                        return false;
                    } else if (error == NotEnoughData)
                        return true; // Will be called later on, when more data are available
                }

                // Handle the request now
                context->client = &client;
                if (!handleRequest(intern, client)) return false;
                return true;
            }
            /** The given socket is now writable. */
            bool clientWritePossible(const BaseSocket &, InternalObject &) { return true; }
            /** Minimum amount of bytes to read before triggering the clientReadPossible callback.
                This is useful for example, if your protocol is fixed size message, you don't have to deal with reconstructing the buffers yourselves.
                This is also useful for textual protocol, where parsing a line can be slow, you can 'prebuffer' a given amount
                For example, in HTTP, 'GET / HTTP/x.x\n\n' is the minimal request, so it might be interesting to set the amount to 16 bytes.
                @return 0 (default) if you want to be triggered as soon as possible. */
            uint32 minimumAmountToRead() const { return 0; }
            /** Maximum lingering time (in second).
                To avoid requests flood, all servers implements a simple timeout based limit.
                If a connection doesn't progress for the returned amount of time (in second), then it's deleted.
                @return 30 (default) if you allow 30s between each progress on the connection. */
            uint32 maxLingerTime() const { return 30; }
            /** The server will call this to check if you want to monitor when a socket is ready.
                You'll unlikely have to change this
                @return true if you want the server to monitor when a socket is writable */
            bool wantToMonitorWrite() { return false; }
            /** Write loop finished callback
                This is called when all the socket in the pool have been written too */
            void writeLoopFinished()
            {
                // do nothing
            }
            /** Return true while the server is running */
            bool isRunning() const { return true; }
            /** Parse the request's first line.
                Textual headers server usually have a "METHOD URL PROTOCOL/VERSION" first line.
                This actually parses this line in the given context, if possible.
                This sets specific query headers like this:
                -# "##REQUEST##" contains the complete request first line
                -# "##METHOD##" contains the method queried
                -# "##VERSION##" contains the version in the request line
                -# "##RESOURCE##" contains the path in the URL
                -# "##URL##" contains the URL asked for
                @param intern   The received object when parsing the request lines
                @param protocol The protocol used for this server including last '/' (for ex: "HTTP/")
                @param line     The first request line to parse */
            bool parseRequestLine(InternalObject & intern, const String & protocol, String line);
            /** Parsing an header line
                @param intern   The received object when parsing the request lines
                @param line     The first request line to parse */
            bool parseHeader(InternalObject & intern, String line);
            /** The server is stopping (this is called before any internal closing action happened) */
            void serverStopping() {}
            /** The server is going to loop once (this is called before any internal loop action happened) */
            void serverLooping() {}


            // Interface
        public:
            /** Parse a client request.
                You must read all available incoming data here, without blocking
                If you must block, you can return NotEnoughData */
            virtual ParsingError parseRequest(InternalObject & intern, const BaseSocket & client) = 0;
            /** Handle the request itself.
                @return false on error, it will immediately close the connection */
            virtual bool handleRequest(InternalObject & intern, const BaseSocket & client) = 0;
            /** Check if the method is allowed */
            virtual bool isMethodSupported(const String & method) const = 0;
            /** Called when needed to change socket options.
                @param client       The socket to change options (if required)
                @param accepting    If true, the socket has just been accepted. If false, then the socket just finished sending its before last packet */
            virtual void changeSocketOptions(BaseSocket & client, const bool accepting) {}

            // Construction and destruction
        public:
            /** Build a textual header server
                @warning The object take ownership of the pool passed in */
            TextualHeadersServer(Network::Socket::MonitoringPool * serverPool) : pool(*serverPool) {}
            virtual ~TextualHeadersServer() { delete (Network::Socket::MonitoringPool *)&pool; }
        };
    }
}

#endif
