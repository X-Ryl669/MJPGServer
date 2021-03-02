#include <time.h>
// We need our declaration
#include "../../include/Network/Connection.hpp"

namespace Network
{
    namespace Connection
    {
        // Get the local socket 
        Network::Socket::Local & Datagram::getLocal() { return local; }
        // Get the local socket 
        const Network::Socket::Local & Datagram::getLocal() const { return local; }
        // Get the remote socket 
        const Network::Socket::Remote & Datagram::getRemote() const { return *remote; }
        // Check if we are connected 
        bool Datagram::isConnected() const { return local.getSocket().getState() >= Network::Socket::BaseSocket::Connecting; }
        // Check if this is a reliable connection method 
        bool Datagram::isReliable() const { return false; }
        // Check if the connection is up
        bool Datagram::isUp() const { return true; }
        // Read data from the remote end 
        int Datagram::read(char * buffer, const int length, const Time::TimeOut & timeout) const
        {
            if (local.getSocket().getState() < Network::Socket::BaseSocket::Bound) return -1;
            if (timeout <= 0) return timeout;
            int receivedLength = 0;
            while (receivedLength < length && timeout > 0)
            {
                if (local.getSocket().select(true, false, timeout))
                {
                    int received = local.getSocket().receive(&buffer[receivedLength], length - receivedLength, 0);
                    timeout.filterError(received);
                    if (received == 0) return receivedLength;
                    if (received < 0) return received;
                    receivedLength += received;
                } else return receivedLength;
            }
            return receivedLength;
        }
        // Send data to the remote end
        int Datagram::send(const char * buffer, const int length, const Time::TimeOut & timeout) const
        {
            if (!isConnected()) return -1;
            if (timeout <= 0) return timeout;
            int sentLength = 0;
            while (sentLength < length && timeout > 0)
            {
                if (local.getSocket().select(false, true, timeout))
                {
                    int sent = local.getSocket().send(&buffer[sentLength], length - sentLength, 0);
                    timeout.filterError(sent);
                    if (sent == 0) return sentLength;
                    if (sent < 0) return sent;
                    sentLength += sent;
                } else return sentLength;
            }
            return sentLength;
        }
        // Optimize common code
        int __readLine__(const Network::Socket::BaseSocket & socket, char * buffer, const int length, const Platform::EndOfLine EOL, const Time::TimeOut & timeout)
        {
            if (timeout <= 0) return timeout;
            int receivedLength = 0;
            bool allowPeeking = true;
            while (receivedLength < length && timeout > 0)
            {
                if (socket.select(true, false, timeout))
                {
                    int received = socket.receive(&buffer[receivedLength], allowPeeking ? (length - receivedLength) : 1, allowPeeking ? MSG_PEEK : 0);
                    if (received == 0 || (received == -1 && socket.getLastError() == Network::Socket::BaseSocket::BadArgument))
                    {   // The socket implementation might disallow peeking, so let's get it now
                        if (allowPeeking)
                        {
                            allowPeeking = false;
                            received = socket.receive(&buffer[receivedLength], 1, 0);
                            timeout.filterError(received);
                            if (received == 0) return -2;
                        }
                        else return -1;
                    }
                    timeout.filterError(received);
                    if (received < 0) return -1;
                    if (!allowPeeking)
                    {
                        if ((EOL == Platform::CR || EOL == Platform::Any) && buffer[receivedLength] == '\r') 
                        { 
                            buffer[receivedLength+1] = 0; return receivedLength+1; 
                        }
                        if ((EOL == Platform::LF || EOL == Platform::Any) && buffer[receivedLength] == '\n') 
                        { 
                            buffer[receivedLength+1] = 0; return receivedLength+1; 
                        }
                        if (EOL == Platform::CRLF && buffer[receivedLength] == '\r') 
                        {
                            if (!socket.select(true, false, timeout)) return -1;
                            received = socket.receive(&buffer[receivedLength+1], 1, 0);
                            timeout.filterError(received);
                            if (received == 0) return -2;
                            if (received < 0) return -1;
                            if (buffer[receivedLength+1] == '\n') 
                            { 
                                buffer[receivedLength+2] = 0;
                                return receivedLength+2; 
                            }
                            receivedLength++;
                        }
                    } else
                    {
                        // Ok, check if we have EOF inside the received data
                        char * foundPos = 0;
                        switch(EOL)
                        {
                        case Platform::CR:
                            foundPos = (char*)memchr(&buffer[receivedLength], '\r', received);
                            break;
                        case Platform::LF:
                            foundPos = (char*)memchr(&buffer[receivedLength], '\n', received);
                            break;
                        case Platform::CRLF:
                            {
                                char * _buffer = &buffer[receivedLength];
                                while (1)
                                {
                                    foundPos = (char*)memchr(_buffer, '\r', received);
                                    if (!foundPos) break;
                                    if (*(foundPos+1) != '\n')
                                    {   // Continue searching
                                        received -= static_cast<int>(foundPos+1 - _buffer);
                                        _buffer = foundPos + 1;
                                    } else foundPos++;
                                    break;
                                }
                            }
                            break;
                        case Platform::Any:
                            {
                                foundPos = (char*)memchr(&buffer[receivedLength], '\r', received);
                                char * foundPosLF = (char*)memchr(&buffer[receivedLength], '\n', received);
                                if (foundPosLF < foundPos) foundPos = foundPosLF;
                            }
                            break;
                        case Platform::AutoDetect:
                            {
                                char * endLine = buffer + receivedLength + received;
                                for (foundPos = &buffer[receivedLength]; foundPos < endLine && *foundPos != '\r' && *foundPos != '\n'; foundPos++)
                                    {}
                                // Search for '\r\n' pattern
                                if (*foundPos == '\r' && foundPos + 1 < endLine && *(foundPos+1) == '\n') foundPos++;
                            }
                            break;
                        }
                        if (foundPos) 
                        {
                            // Compute the real receive size 
                            received = (int)(foundPos - buffer);
                            // And receive this now
                            socket.receive(buffer, receivedLength + received + 1, 0);
                            buffer[receivedLength + received + 1] = 0;
                            return receivedLength + received + 1;
                        }
                    }
                    receivedLength += received;
                } else return -1;
            }
            return -1;
        }

        // Read a line from the remote end.
        int Datagram::readLine(char * buffer, const int length, const EndOfLine EOL, const Time::TimeOut & timeout) const
        {
            if (local.getSocket().getState() < Network::Socket::BaseSocket::Bound) return -1;
            int ret = __readLine__(local.getSocket(), buffer, length, EOL, timeout);
            return ret < 0 ? -1 : ret; // Filters-out end of stream errors
        }
        // Break this connection. 
        bool Datagram::breakConnection()
        {
            if (ownLocal)
            {
                local.close();
                delete &local;
                ownLocal = false;
            }
            if (ownRemote) 
            {
                Network::Socket::Remote *& _remote = const_cast<Network::Socket::Remote*&>(remote); 
                delete _remote;
                _remote = 0;
                ownRemote = false;
            }
            return true;
        }
        
        // Establish a connection to the given address (using default port and address).
        Datagram * Datagram::buildConnection(const Address & remoteAddress, const Time::TimeOut & timeout, Address * localAddress)
        {
            Network::Socket::Local * local = new Network::Socket::Local(localAddress, new Network::Socket::BerkeleySocket(Network::Socket::BaseSocket::Datagram));
            if (!local) return 0;
            if (!local->connectTo(remoteAddress)) { delete local; return 0; }
            BaseConnection * base = local->isConnectedToRemote(timeout);
            if (!base) { delete local; return 0; }
            Datagram * datagram = 0;
            if (base->getType() == 1)
                datagram = (Datagram*)(base);

            if (!datagram) { delete local; return 0; }
            // Ok, tell this connection that we are owning the local socket now
            datagram->ownLocal = true;
            return datagram;
        }



        // Get the local socket 
        Network::Socket::Local & Stream::getLocal() { return local; }
        // Get the local socket 
        const Network::Socket::Local & Stream::getLocal() const { return local; }
        // Get the remote socket 
        const Network::Socket::Remote & Stream::getRemote() const { return *remote; }
        // Check if we are connected 
        bool Stream::isConnected() const { return local.getSocket().getState() >= Network::Socket::BaseSocket::Connecting; }
        // Check if this is a reliable connection method 
        bool Stream::isReliable() const { return true; }
        // Check if the connection is running
        bool Stream::isUp() const { return !hadFailed && isConnected(); }
        // Read data from the remote end 
        int Stream::read(char * buffer, const int length, const Time::TimeOut & timeout) const
        {
            if (!isConnected()) return -1;
            if (timeout <= 0) return timeout;
            int receivedLength = 0;
            while (receivedLength < length && timeout > 0)
            {
                if (local.getSocket().select(true, false, timeout))
                {
                    int received = local.getSocket().receive(&buffer[receivedLength], length - receivedLength, 0);
                    timeout.filterError(received);
                    if (received == 0) { hadFailed = true; return receivedLength; }
                    if (received < 0) return received;
                    receivedLength += received;
                } else return receivedLength;
            }
            return receivedLength;
        }
        // Send data to the remote end
        int Stream::send(const char * buffer, const int length, const Time::TimeOut & timeout) const
        {
            if (!isConnected()) return -1;
            if (timeout <= 0) return timeout;
            int sentLength = 0;
            while (sentLength < length && timeout > 0)
            {
                if (local.getSocket().select(false, true, timeout))
                {
                    int sent = local.getSocket().send(&buffer[sentLength], length - sentLength, 0);
                    timeout.filterError(sent);
                    if (sent == 0) return sentLength;
                    if (sent < 0) { hadFailed = true; return sent; }
                    sentLength += sent;
                } else return sentLength;
            }
            return sentLength;
        }
        // Read a line from the remote end.
        int Stream::readLine(char * buffer, const int length, const EndOfLine EOL, const Time::TimeOut & timeout) const
        {
            if (!isConnected()) return -1;
            int ret = __readLine__(local.getSocket(), buffer, length, EOL, timeout);
            if (ret == -2) hadFailed = true;
            return ret < 0 ? -1 : ret; // Filters out end of stream error
        }
        // Break this connection. 
        bool Stream::breakConnection()
        {
            if (ownLocal)
            {
                local.close();
                delete &local;
                ownLocal = false;
            }
            if (ownRemote) 
            {
                Network::Socket::Remote *& _remote = const_cast<Network::Socket::Remote*&>(remote); 
                delete _remote;
                _remote = 0;
                ownRemote = false;
            }
            return true;
        }
        
        // Establish a connection to the given address (using default port and address).
        Stream * Stream::buildConnection(const Address & remoteAddress, const Time::TimeOut & timeout, Address * localAddress)
        {
            Network::Socket::Local * local = new Network::Socket::Local(localAddress, new Network::Socket::BerkeleySocket(Network::Socket::BaseSocket::Stream));
            if (!local) return 0;
            if (!local->connectTo(remoteAddress)) { delete local; return 0; }
            BaseConnection * base = local->isConnectedToRemote(timeout);
            if (!base) { delete local; return 0; }
            Stream * stream = 0;
            if (base->getType() == 2)
                stream = (Stream*)(base);
            
            if (!stream) { delete local; return 0; }
            // Ok, tell this connection that we are owning the local socket now 
            stream->ownLocal = true;
            return stream;
        }

#if (WantSSLCode == 1)
        // Build a connection to the given address, using SSL whenever required.
        Stream * Stream::buildConnection(const Address & remoteAddress, Crypto::SSLContext & context, const Time::TimeOut & timeout, Address * localAddress, Network::Socket::SSL_TLS::CertificateValidator * validator)
        {
            Network::Socket::BaseSocket * socket = 0;
            if (remoteAddress.isProtocolUsingSSL(remoteAddress.getProtocol()))
            {
                socket = new Network::Socket::SSL_TLS(context, Network::Socket::BaseSocket::Stream);
                if (!socket) return 0;
                if (validator) ((Network::Socket::SSL_TLS*)socket)->setCertificateValidator(validator);
            } else 
            {
                socket = new Network::Socket::BerkeleySocket(Network::Socket::BaseSocket::Stream);
                delete validator;
            }
            Network::Socket::Local * local = new Network::Socket::Local(localAddress, socket);
            if (!local) return 0;
            if (!local->connectTo(remoteAddress)) { delete local; return 0; }

            BaseConnection * base = local->isConnectedToRemote(timeout);
            if (!base) { delete local; return 0; }
            Stream * stream = 0;
            if (base->getType() == 2)
                stream = (Stream*)(base);

            if (!stream) { delete local; return 0; }
            // Ok, tell this connection that we are owning the local socket now
            stream->ownLocal = true;
            return stream;
        }
#endif
    }
}
