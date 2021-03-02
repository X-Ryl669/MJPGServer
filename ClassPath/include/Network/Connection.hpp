#ifndef hpp_CPP_Connection_CPP_hpp
#define hpp_CPP_Connection_CPP_hpp

// We need address declarations
#include "Address.hpp"
// We need socket declarations
#include "Socket.hpp"

#if (WantSSLCode == 1)
// If you want SSL code, we need the declaration
#include "SSLSocket.hpp"
#endif

// We need platform specific structure
#include "../Platform/Platform.hpp"

/** Network specific code, like socket classes declaration and others */
namespace Network
{
    /** Tools to create connections using any types of sockets.

        Using connection is easier than dealing with socket themselves.
        There are multiple code pattern that appears in socket programming that 
        are implemented in connection. 

        For example, instead of writing:
        @code
            // Read the given amount of data from a socket, not blocking, handling all possible error 
            int len = 0; int expectedLen = 4096;
            while (len < expectedLen && socket.select(true, false))
            {
                int result = socket.receive(&buffer[len], expectedLen - len);
                if (result <= 0) break;
                len += result;
            }
            if (len < expectedLen) return Error;
        @endcode

        You'll write:
        @code
            // From a connection
            if (connection.read(buffer, expectedLen) <= 0) return Error;
        @endcode

        @sa BaseConnection.
        */
    namespace Connection
    {
        /** Shortcut to the base address interface */
        typedef Address::BaseAddress Address;
        /** Shortcut to the base socket interface */
        typedef Socket::BaseSocket Socket;
        
        /** The base interface for a connection. 
            Using a connection simplifies development.

            For example, instead of writing:
            @code
                // Read the given amount of data from a socket, not blocking, handling all possible error 
                int len = 0; int expectedLen = 4096;
                while (len < expectedLen && socket.select(true, false))
                {
                    int result = socket.receive(&buffer[len], expectedLen - len);
                    if (result <= 0) break;
                    len += result;
                }
                if (len < expectedLen) return Error;
            @endcode

            You'll write:
            @code
                // From a connection
                if (connection.read(buffer, expectedLen) <= 0) return Error;
            @endcode

            Or for reading a "\r\n"(CRLF) ended line, 
            @code
                // From a connection
                if (connection.readLine(buffer, expectedLen) <= 0) return Error;
                
            @endcode
        */
        struct BaseConnection
        {
            // Type definition and enumeration
        public:
            /** The end of line marker */
            typedef Platform::EndOfLine EndOfLine;
            /** Get the local socket */
            virtual Network::Socket::Local & getLocal() = 0;
            /** Get the local socket */
            virtual const Network::Socket::Local & getLocal() const = 0;
            /** Get the remote socket */
            virtual const Network::Socket::Remote & getRemote() const = 0;
            /** Get the connection type.
                This is used as a poor man RTTI for platform without RTTI. */
            virtual int getType() = 0;

            /** Check if we are connected */
            virtual bool isConnected() const = 0;
            /** Check if this is a reliable connection method */
            virtual bool isReliable() const = 0;
            /** Check if the connection is up.
                This always returns true on a Datagram connection (this there is no "up" state).
                For Stream connection, it can only reply what the kernel tell it. 
                Typically, you can write a lot of data on a "down" connection, and the real status of the connection is only visible numerous minutes after (since the kernel must retransmit with exponential backoff).
                @warning It's useless in the case when you want to know the instant status of the connection, so don't use this in this case, you need to set up a kind of "ping" feature in the upper level.
                However, when the connection failed at some point, it worth remembering it so you can assert the connection is good / bad *after* it was seen as such and *before* you'd use it wrongly */
            virtual bool isUp() const = 0;
            /** Read data from the remote end.
                This method will do its best to fill the length data in the buffer.
                However, it's possible that the asked length isn't available while the timeout expires, or the socket errors.

                If you need to glob, then create a timeout with autorefilling.
                @verbatim
                Possible multiple timeout explanation (if glob is requested):
                    1) Let's say the remote side send 1 byte per sec
                    2) You call "connection.read(buffer, 32, 2000);"
                    3) If 32 bytes are already available, you'll get immediate result
                    4) Else, the call will wait for 2s, but will advance at 1s (receiving 1 byte), so it will restart waiting mode for 2s and so on until 32 bytes are received.
                    5) In worst case, you'll return from this method (length * timeout) ms after the call.
                @endverbatim
                @warning If you ask for more data than available, it's possible to accumulate multiple timeout (see section above).
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param timeout  The read timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout) 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int read(char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const = 0;
            /** Send data to the remote end 
                This method will do its best to send the length data in the buffer.
                However, it's possible that the asked length isn't available while the timeout expires, or the socket errors.

                If you need to glob, then create a timeout with autorefilling.
                @verbatim
                Possible multiple timeout explanation (if glob is requested):
                    1) Let's say the remote side receive 1 byte per sec
                    2) You call "connection.send(buffer, 32, 2000);"
                    3) If 32 bytes are already available, you'll get immediate result
                    4) Else, the call will wait for 2s, but will advance at 1s (sending 1 byte), so it will restart waiting mode for 2s and so on until 32 bytes are sent.
                    5) In worst case, you'll return from this method (length * timeout) ms after the call.
                @endverbatim

                @param buffer   The buffer to send (buffer must be at least length bytes)
                @param length   The length of the data to send
                @param timeout  The send timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout) 
                @param glob     If true, the timeout is not considered absolute, and the call will return, at worst, after the timeout 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int send(const char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const = 0;
            /** Read a line from the remote end.
                The line returned contains the EndOfLine marker.
                @warning The default method calls read byte per byte, so it's ok for small line, but not for a line search in a big binary buffer
                @warning The Berkeley socket usually allow smarted search in received buffer, so it's safer to use
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param EOL      The allowed end of line marker (@sa EndOfLine)
                @param timeout  The read timeout in millisecond (use negative for infinite timeout) 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int readLine(char * buffer, const int length, const EndOfLine EOL = Platform::CRLF, const Time::TimeOut & timeout = DefaultTimeOut) const = 0;
            /** Purge the remote end buffer.
                It's a usual pattern to have the remote end that's sending more data than what we really need, 
                and you need to purge this data before you can send the next round data.
                
                In that case, you can try to read them in buffer you'll throw away once read, but this leads 
                to common code in all the protocol part of your code.
                This methods just does that, it reads data from the remote end with a (small) timeout 
                (trashing the read data), until the read times out. 
                @return The number of byte trashed (or 0 on socket closed, -1 on error) */
            virtual int purge(const Time::TimeOut & timeout = DefaultTimeOut) const { char Buffer[1024]; int s, cumSize = 0; while ((s = read(Buffer, sizeof(Buffer), timeout)) == sizeof(Buffer)) cumSize += s; return s <= 0 ? s : cumSize + s; }
                
            /** Break this connection. */
            virtual bool breakConnection() = 0;


            /** Required destructor */
            virtual ~BaseConnection() {};
        };

        /** A datagram based connection */
        struct Datagram : public BaseConnection
        {
            // Members
        private:
            /** The local socket */
            Network::Socket::Local &        local;
            /** The remote socket */
            const Network::Socket::Remote * remote;
            /** Do we own the remote socket ? */
            bool                            ownRemote;
            /** Do we own the local socket ? */
            bool                            ownLocal;

            // BaseConnection interface
        public:    
            /** Get the local socket */
            virtual Network::Socket::Local & getLocal();
            /** Get the local socket */
            virtual const Network::Socket::Local & getLocal() const;
            /** Get the remote socket */
            virtual const Network::Socket::Remote & getRemote() const;
            /** Check if we are connected */
            virtual bool isConnected() const;
            /** Check if this is a reliable connection method */
            virtual bool isReliable() const;
            /** Check if the connection is up.
                This always returns true on a Datagram connection (this there is no "up" state).
                For Stream connection, it can only reply what the kernel tell it. 
                Typically, you can write a lot of data on a "down" connection, and the real status of the connection is only visible numerous minutes after (since the kernel must retransmit with exponential backoff).
                @warning Since it's useless in that case you wan to know the instant status of the connection, don't use this in this case, you need to set up a kind of "ping" feature in the upper level.
                However,  when the connection failed at some point, it worth remembering it so you can assert the connection is good / bad *after* it was seen as such and *before* you'd use it wrongly */
            virtual bool isUp() const;
            /** Get the connection type.
                This is used as a poor man RTTI for platform without RTTI. */
            virtual int getType() { return 1; }
        
            /** Read data from the remote end.
                If you need to glob, then create a timeout with autorefilling.
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param timeout  The read timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout)
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int read(char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Send data to the remote end 
                If you need to glob, then create a timeout with autorefilling.
                @param buffer   The buffer to send (buffer must be at least length bytes)
                @param length   The length of the data to send
                @param timeout  The send timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout) 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int send(const char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Read a line from the remote end.
                @warning The default method calls read byte per byte, so it's ok for small line, but not for a line search in a big binary buffer
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param EOL      The allowed end of line marker (@sa EndOfLine)
                @param timeout  The read timeout in millisecond (use negative for infinite timeout) 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int readLine(char * buffer, const int length, const EndOfLine EOL = Platform::CRLF, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Break this connection.
                @warning You can't do anything with this connection once it's broken. */
            virtual bool breakConnection();
            
            /** Establish a connection to the given address (using default port and address).

                For a datagram socket, this method is only used to traverse a NAT or firewall, 
                no actual useful data is sent on the remote end.

                @param remoteAddress    The remote address to connect to (the connection doesn't owns the given reference)
                @param timeout          The timeout to try to connect to
                @param localAddress     If set, the local address to use
                */
            static Datagram * buildConnection(const Address & remoteAddress, const Time::TimeOut & timeout = DefaultTimeOut, Address * localAddress = 0);

            Datagram(Network::Socket::Local & local, const Network::Socket::Remote & remote) : local(local), remote(&remote), ownRemote(false), ownLocal(false)  { }
            Datagram(Network::Socket::Local & local, Network::Socket::Address * address) : local(local), remote(new Network::Socket::Remote(address)), ownRemote(true), ownLocal(false)  { }
            Datagram(Network::Socket::Local * local, Network::Socket::Address * address) : local(*local), remote(new Network::Socket::Remote(address)), ownRemote(true), ownLocal(true)  { }
            ~Datagram() { breakConnection(); }
        };



        /** A stream based connection */
        struct Stream : public BaseConnection
        {
            // Members
        private:
            /** The local socket */
            Network::Socket::Local &        local;
            /** The remote socket */
            const Network::Socket::Remote * remote;
            /** Do we own the remote socket ? */
            bool                            ownRemote;
            /** Do we own the local socket ? */
            bool                            ownLocal;
            /** Did the connection failed at some point ? */
            mutable bool                    hadFailed;

            // BaseConnection interface
        public:    
            /** Get the local socket */
            virtual Network::Socket::Local & getLocal();
            /** Get the local socket */
            virtual const Network::Socket::Local & getLocal() const;
            /** Get the remote socket */
            virtual const Network::Socket::Remote & getRemote() const;
            /** Check if we are connected */
            virtual bool isConnected() const;
            /** Check if this is a reliable connection method */
            virtual bool isReliable() const;
            /** Check if the connection is up.
                This always returns true on a Datagram connection (this there is no "up" state).
                For Stream connection, it can only reply what the kernel tell it. 
                Typically, you can write a lot of data on a "down" connection, and the real status of the connection is only visible numerous minutes after (since the kernel must retransmit with exponential backoff).
                @warning Since it's useless in that case you wan to know the instant status of the connection, don't use this in this case, you need to set up a kind of "ping" feature in the upper level.
                However,  when the connection failed at some point, it worth remembering it so you can assert the connection is good / bad *after* it was seen as such and *before* you'd use it wrongly */
            virtual bool isUp() const;

            /** Get the connection type.
                This is used as a poor man RTTI for platform without RTTI. */
            virtual int getType() { return 2; }
        
            /** Read data from the remote end 
                If you need to glob, then create a timeout with autorefilling.
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param timeout  The read timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout)
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int read(char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Send data to the remote end 
                If you need to glob, then create a timeout with autorefilling.
                @param buffer   The buffer to send (buffer must be at least length bytes)
                @param length   The length of the data to send
                @param timeout  The read timeout in millisecond (use Time::TimeOut::Infinite for infinite timeout)
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int send(const char * buffer, const int length, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Read a line from the remote end.
                @warning The default method calls read byte per byte, so it's ok for small line, but not for a line search in a big binary buffer
                @param buffer   The buffer to read into (buffer must be at least length bytes)
                @param length   The length of the data to read into buffer 
                @param EOL      The allowed end of line marker (@sa EndOfLine)
                @param timeout  The read timeout in millisecond (use negative for infinite timeout)
                @warning The method is reliable, it only either return a full line, or an error. 
                @warning To achieve such reliability, the timeout is globbed, it can be overcome 
                @return the number of current bytes read, 0 on socket closed, and negative on error */
            virtual int readLine(char * buffer, const int length, const EndOfLine EOL = Platform::CRLF, const Time::TimeOut & timeout = DefaultTimeOut) const;
            /** Break this connection. */
            virtual bool breakConnection();
            
            /** Establish a connection to the given address (using default port and address).

                For a datagram socket, this method is only used to traverse a NAT or firewall, 
                no actual useful data is sent on the remote end.

                @param remoteAddress    The remote address to connect to (the connection doesn't owns the given reference)
                @param timeout          The timeout to try to connect to
                @param localAddress     If set, the local address to use
                @return A pointer on a new allocated stream or 0 on error */
            static Stream * buildConnection(const Address & remoteAddress, const Time::TimeOut & timeout = DefaultTimeOut, Address * localAddress = 0);

// Only define and declare this method if the SSL socket code was included first            
#ifdef HasSSLSocket            
            /** Build a connection to the given address, using SSL whenever required.

                For a datagram socket, this method is only used to traverse a NAT or firewall, 
                no actual useful data is sent on the remote end.

                @param remoteAddress    The remote address to connect to (the connection doesn't owns the given reference)
                @param timeout          The timeout to try to connect to
                @param localAddress     If set, the local address to use
                @param sslContext       The SSL context to use, if the remote address requires using SSL (the context is not owned)
                @param validator        If valid and the socket is indeed a SSL socket, this validator is used instead of the default one (it's owned by the socket)
                @return A pointer on a new allocated stream or 0 on error */
            static Stream * buildConnection(const Address & remoteAddress, Crypto::SSLContext & context, const Time::TimeOut & timeout = DefaultTimeOut, Address * localAddress = 0, Network::Socket::SSL_TLS::CertificateValidator * validator = 0);
#endif            

            Stream(Network::Socket::Local & local, const Network::Socket::Remote & remote) : local(local), remote(&remote), ownRemote(false), ownLocal(false), hadFailed(false)  { }
            Stream(Network::Socket::Local & local, Network::Socket::Address * address) : local(local), remote(new Network::Socket::Remote(address)), ownRemote(true), ownLocal(false), hadFailed(false)  { }
            Stream(Network::Socket::Local * local, Network::Socket::Address * address) : local(*local), remote(new Network::Socket::Remote(address)), ownRemote(true), ownLocal(true), hadFailed(false)  { }
            ~Stream() { breakConnection(); }

        };

        /** A pseudo stream based connection */
        struct PseudoStream : public Stream
        {
            /** Establish a connection to the given address (using default port and address).

                For a datagram socket, this method is only used to traverse a NAT or firewall, 
                no actual useful data is sent on the remote end.

                @param remoteAddress    The remote address to connect to (the connection owns the given pointer)
                @param timeout          The timeout to try to connect to
                @param localAddress     If set, the local address to use
                */
            static PseudoStream * buildConnection(const Address * remoteAddress, const Time::TimeOut & timeout = DefaultTimeOut, Address * localAddress = 0);

            // Hide the default constructor
        private:
            PseudoStream() : Stream(0, 0) {}
        };
    }
}

#endif
