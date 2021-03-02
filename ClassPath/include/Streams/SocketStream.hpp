#ifndef hpp_SocketStream_hpp
#define hpp_SocketStream_hpp

// We need stream declarations
#include "Streams.hpp"
// We need socket declaration too
#include "../Network/Socket.hpp"

/** @internal */
namespace Network { namespace Connection { int __readLine__(const Network::Socket::BaseSocket & socket, char * buffer, const int length, const Platform::EndOfLine EOL, const Time::TimeOut & timeout); } }

namespace Stream
{
    /** Socket based input stream. 
        Please notice that the socket must be in blocking mode while using this stream, as 
        the stream doesn't know how to deal with socket errors. */
    class SocketInputStream : public LineSplitStream<Strings::FastString>
    {
        // Members
    private:
        /** The socket to read from */
        const Network::Socket::BaseSocket & socket;
        /** If set, set the amount of data to read (might be -1 for infinite) */
        mutable uint64                amount;
        /** Get the amount of data read */
        mutable uint64                readBytes;
        /** Set to true if you want direct reading. */
        bool                          direct;
        /** The timeout for socket operation in millisecond */
        int                           timeout;

        // Interface        
    public:
        /** Try to read the given amount of data to the specified buffer
            @return the number of byte truly read (this method doesn't throw) */
        inline uint64 read(void * const buffer, const uint64 size) const throw() 
        {
            uint64 done = 0;
            while (done < size)
            {
                int sizeToRead = (int)min(65536, (int)min(size - done, amount));
                if (direct || socket.select(true, false, timeout))
                {   // Socket should be blocking anyway, but at least release some CPU time while waiting for the data
                    int received = socket.receive((char* const)buffer + done, sizeToRead, 0);
                    if (amount != (uint64)-1) amount -= (uint64)max(0, received);
                    if ((direct && received < sizeToRead) || received <= 0) break; // Error or end of stream
                    done += received;
                } 
                // No event in the specified timeout, so we'll stop the read anyway
                else break;
            }
            readBytes += done;
            return done; 
        } 
        /** Move the stream position forward of the given amount
            This should give the same results as setPosition(currentPosition() + value),
            but implementation can be faster for non-seek-able stream. */
        inline bool goForward(const uint64 skip)
        {
            uint8 buffer[128];
            uint64 done = 0;
            while (done < skip)
            {
                uint64 bytes = read(buffer, min(skip, (uint64)128));
                if (bytes == 0) return false;
                readBytes += bytes;
                done += bytes;
            }
            return true;
        }
        /** This method returns the stream length in byte, if known
            If the length is equal or higher than 2^32 - 1, the returned value is (uint64)-2
            For stream where the length is not known, this method will return (uint64)-1. */
        inline uint64 fullSize() const { return amount; }
        /** This method returns true if the end of stream is reached */
        inline bool endReached() const { return amount != (uint64)-1 && (int64)amount <= 0; }
        /** This method returns the position of the next byte that could be read from this stream */
        inline uint64 currentPosition() const { return readBytes; }
        /** Try to seek to the given absolute position (return false if not supported) */
        inline bool setPosition(const uint64) { return false; }
        /** Read the next line
            @return The String containing the next line (should end by LF or CRLF) */
        inline Strings::FastString readNextLine() const throw()   { return readNextLine(Platform::Any); }
        /** Specialized version with specified end of line */
        inline Strings::FastString readNextLine(Platform::EndOfLine eol) const throw()   
        {
            char buffer[4096]; int length = sizeof(buffer);
            int len = Network::Connection::__readLine__(socket, buffer, length, eol, timeout);
            if (len <= 0) return "";
            readBytes += len;
            return Strings::FastString(buffer, len);
        }
        /** Set the timeout to use for reading */
        inline void setTimeout(const int timeOutMillisecond = Network::DefaultTimeOut) { timeout = timeOutMillisecond; }
        
        // Construction and destruction 
    public:
        /** Construct a SocketInputStream object from the given socket with an amount of bytes to read.
            @param socket       The socket to read from. This must lives as long as this object is used.
            @param maxAmount    The maximum number of bytes to read from the socket (default is to takes as much as possible) 
            @param direct       If true, no status checking is done on the socket before reading it, it'll block if no data is available.
                                If false, the socket is checked if it's readable (with the given timeout), and short read if not */
        SocketInputStream(const Network::Socket::BaseSocket & socket, const uint64 maxAmount = (uint64)-1, const bool direct = false) 
            : socket(socket), amount(maxAmount), readBytes(0), direct(direct), timeout(Network::DefaultTimeOut) {}
    };
    
    /** Socket based output stream. 
        Please notice that the socket must be in blocking mode while using this stream, as 
        the stream doesn't know how to deal with socket errors. */
    class SocketOutputStream : public OutputStream
    {
        // Members
    private:
        /** The socket to write to */
        Network::Socket::BaseSocket & socket;
        /** The amount written */
        uint64  amount; 
        /** Set to true if you want direct writing. */
        bool    direct;
        /** The timeout for socket operation in millisecond */
        int     timeout;
        
        // Interface        
    public:
        /** This method returns the stream length in byte, if known
            If the length is equal or higher than 2^32 - 1, the returned value is 0xfffffffe */
        virtual uint64 fullSize() const  { return amount; }
        /** This method returns true if the end of stream is reached */
        virtual bool endReached() const  { return true; }
        /** This method returns the position of the next byte that could be read from this stream */
        virtual uint64 currentPosition() const { return fullSize(); }
        /** Try to seek to the given absolute position (return false if not supported) */
        virtual bool setPosition(const uint64) { return false; }
        /** Move the stream position forward of the given amount
            This should give the same results as setPosition(currentPosition() + value),
            but implementation can be faster for non-seek-able stream. */
        virtual bool goForward(const uint64) { return false; }
        /** Try to write the given amount of data to the specified buffer
            @return the number of byte truly written (this method doesn't throw) */
        virtual uint64 write(const void * const buffer, const uint64 size) throw() 
        { 
            uint64 done = 0;
            while (done < size)
            {
                int sizeToWrite = (int)min((uint64)65536, size - done);
                if (direct || socket.select(false, true, timeout))
                {   // Socket should be blocking anyway, but at least release some CPU time while waiting for the buffer to empty
                    int written = socket.send((const char * const)buffer + done, sizeToWrite, 0);
                    amount += max(0, written);
                    if ((direct && written <= sizeToWrite) || written <= 0) break;;
                    done += sizeToWrite;               
                }
                // If the timeout event fired, let's stop the sending process
                else break;
            }
            return done;
        }
        /** Set the timeout to use for writing */
        inline void setTimeout(const int timeOutMillisecond = Network::DefaultTimeOut) { timeout = timeOutMillisecond; }
           
        // Construction and destruction 
    public:
        /** Construct a SocketOutputStream object from the given socket.
            @param socket       The socket to write to. This must lives as long as this object is used.
            @param direct       If true, no status checking is done on the socket before writing to it, it'll block if buffer is full.
                                If false, the socket is checked if it's writable (with the given timeout), and short write if not */
        SocketOutputStream(Network::Socket::BaseSocket & socket, const bool direct = false) : socket(socket), amount(0), direct(direct), timeout(Network::DefaultTimeOut) {}
    };
}


#endif
