// We need our declaration
#include "../../include/Network/Socket.hpp"
// We need connections too
#include "../../include/Network/Connection.hpp"
// We need threading too
#include "../../include/Threading/Threads.hpp"
// We need containers too
#include "../../include/Container/Container.hpp"

#if DEBUG==1
  // We need scope ptr too
  #include "../../include/Utils/ScopePtr.hpp"
  // And hex dumping
  #include "../../include/Utils/Dump.hpp"
#endif

#ifdef _WIN32
    #include <Mswsock.h>
    // We need if_nametoindex
    #include <Iphlpapi.h>
    // Add this to the linker line
    #pragma comment(lib, "ws2_32.lib")

    BOOL MyTransmitPackets(SOCKET hSocket, LPTRANSMIT_PACKETS_ELEMENT lpPacketArray, DWORD nElementCount, DWORD nSendSize = 0, LPOVERLAPPED lpOverlapped = NULL, DWORD dwFlags = TF_USE_DEFAULT_WORKER)
    {
        if(hSocket == INVALID_SOCKET) return false;
        static LPFN_TRANSMITPACKETS pfnTransmitPackets = NULL;
        if (pfnTransmitPackets == NULL)
        {
            GUID guid = WSAID_TRANSMITPACKETS;

            DWORD codeCB = 0;
            DWORD pointerFunctionSize = sizeof(pfnTransmitPackets);
            int error1 = WSAIoctl(hSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, (void *)&guid, sizeof(guid), &pfnTransmitPackets, pointerFunctionSize, &codeCB, 0, 0);
            if(error1) return false;
            if (pfnTransmitPackets == NULL)
            {
                ::WSASetLastError(WSAEINVAL);
                return false;
            }
        }
        BOOL result = pfnTransmitPackets(hSocket, lpPacketArray, nElementCount, nSendSize, lpOverlapped, dwFlags);
        // The result can be false but not linked to a real error, if the Pending process is active (doesn't wait until sending is done to return)
        // To avoid the problem of logic in the code of waiting thread, return TRUE in these case
        if(!result && (WSAGetLastError() == ERROR_IO_PENDING)) return TRUE;
        return result;
    }

    struct AutoRegisterSocket
    {
        HMODULE hMSWSock;
        AutoRegisterSocket()
        {
            WSADATA WsaDat;
            WSAStartup(MAKEWORD(2, 2), &WsaDat);

            hMSWSock = LoadLibrary(_T("MSWSOCK.DLL"));
        }
        ~AutoRegisterSocket()
        {
            FreeLibrary(hMSWSock);
            WSACleanup();
        }
    };

    static AutoRegisterSocket autoregistersocket;
#elif defined(_LINUX)
    // We need epoll for fast client monitoring
    #include <sys/epoll.h>
    // We need send file
    #include <sys/sendfile.h>
    // We need TCP cork too
    #include <netinet/tcp.h>
    // We need fstat
    #include <sys/stat.h>
    // We need if_nametoindex
    #include <net/if.h>
    // We need writev
    #include <sys/uio.h>
    // We need raw Ethernet code too
    #include <sys/ioctl.h>
    #include <netinet/ether.h>
    #include <netinet/ip.h>
    #include <netinet/ip_icmp.h>
    #include <netinet/ether.h>
    #include <netinet/in.h>
    // We also need sockaddr_ll
    #include <linux/if_packet.h>
#elif defined(_MAC)
    #define _DARWIN_C_SOURCE
    // We need TCP cork too there
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    // We need if_nametoindex
    #include <net/if.h>
#endif
namespace Network
{
    namespace Socket
    {
        NameSocket::NameSocket(Address * address)
            : address(address ? *address : *(address = new Network::Address::IPV4()))
        {}

        /** Set the error from the usual int based error code. */
        int BaseSocket::setError(int errorCode) const
        {
            if (errorCode == -1)
            {
    #ifdef _WIN32
                switch (WSAGetLastError())
                {
                case 0: lastError = Success; return errorCode;
                case WSAEINPROGRESS:
                case WSAEWOULDBLOCK: lastError = InProgress; return errorCode;
                case WSAECONNREFUSED: lastError = Refused; return errorCode;
                case WSAEALREADY:
                case WSAEADDRINUSE: lastError = StillInUse; return errorCode;
                case WSAEINVAL: lastError = BadArgument; return errorCode;
                default: lastError = OtherError; return errorCode;
                }
    #else
                switch (errno)
                {
                case 0: lastError = Success; return errorCode;
                case EWOULDBLOCK:
                case EINPROGRESS: lastError = InProgress; return errorCode;
                case ECONNREFUSED: lastError = Refused; return errorCode;
                case EALREADY:
                case EADDRINUSE: lastError = StillInUse; return errorCode;
                case EINVAL: lastError = BadArgument; return errorCode;
                default: lastError = OtherError; return errorCode;
                }
    #endif
            }
            lastError = Success;
            return errorCode;
        }

        BaseSocket::Error BaseSocket::setError(const BaseSocket::Error useSystem) const
        {
            return lastError = useSystem;
        }

        // Send data on the socket reliably
        int BaseSocket::sendReliably(const char * buffer, const int bufferSize, const unsigned int _splitSize, const Time::TimeOut & timeout) const
        {
            int len = 0;
            while (len < bufferSize && select(false, true, timeout))
            {
                int splitSize = _splitSize ? (int)min(_splitSize, (unsigned int)(bufferSize - len)) : (bufferSize - len);
                int result = send(&buffer[len], splitSize, 0);
                if (result < 0)
                {
                    return -1;
                }
                if (result == 0)
                {
                    return len;
                }
                len += result;
            }
            if (len == 0)
            {
                lastError = InProgress;
                return -1;
            }
            return len;
        }
        // Receive data from the socket reliably
        int BaseSocket::receiveReliably(char * buffer, const int bufferSize, const Time::TimeOut & timeout) const
        {
            int len = 0;
            while (len < bufferSize && select(true, false, timeout))
            {
                int splitSize = (unsigned int)(bufferSize - len);
                int result = receive(&buffer[len], splitSize, 0);
                if (result < 0)
                {
                    return -1;
                }
                if (result == 0)
                {
                    return len;
                }
                len += result;
            }
            if (len == 0)
            {
                lastError = InProgress;
                return -1;
            }
            return len;
        }


        // Append a socket to this pool
        bool BerkeleyPool::appendSocket(BaseSocket * _socket)
        {
            if (!_socket || _socket->getTypeID() != 1) return false;
            if (size >= MaxQueueLen) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);
            BerkeleySocket ** newPool = (BerkeleySocket **)Platform::safeRealloc(pool, (size+1) * sizeof(*pool));
            if (newPool == NULL) return false;
            pool = newPool;
            pool[size++] = socket;
            return true;
        }
        // Forget a socket from the pool
        bool BerkeleyPool::forgetSocket(BaseSocket * _socket)
        {
            if (!_socket || _socket->getTypeID() != 1) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);

            // Need to find out the socket in the pool
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket)
                {   // Found
                    FD_ZERO(&rset); FD_ZERO(&wset);
                    // Swap the value with the last on in the list
                    pool[i] = pool[size - 1];
                    size--;
                    pool[size] = 0;
                    // We don't realloc smaller anyway
                    return true;
                }
            }
            return false;
        }

        // Remove a socket from the pool
        bool BerkeleyPool::removeSocket(BaseSocket * _socket)
        {
            if (!forgetSocket(_socket)) return false;
            if (own) delete (BerkeleySocket*)_socket;
            return true;
        }
        // Get the pool size
        uint32 BerkeleyPool::getSize() const { return size; }


        // Select the pool for at least an element that is ready
        bool BerkeleyPool::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
        {
            if (timeout < 0) return false; // Already timed out previously
            // Need to clear the sets, and start
            FD_ZERO(&rset); FD_ZERO(&wset);
            if (reading)
                for (uint32 i = 0; i < size; i++) { if (pool[i]->descriptor != BerkeleySocket::SocketError) FD_SET(pool[i]->descriptor, &rset); }
            if (writing)
                for (uint32 i = 0; i < size; i++) { if (pool[i]->descriptor != BerkeleySocket::SocketError) FD_SET(pool[i]->descriptor, &wset); }

            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;

            int ret = 0;
            // Then select
            while ((ret = ::select(FD_SETSIZE, reading ? &rset : NULL, writing ? &wset : NULL, NULL, timeout < 0 ? NULL : &tv)) == -1)
            {
#ifdef _WIN32
                if (WSAGetLastError() != WSAEINTR) { timeout.failed(-WSAGetLastError()); return false; }
#else
                if (errno != EINTR) { timeout.failed(-errno); return false; }
#endif
                if (timeout.timedOut()) return false;
            }
            timeout.filterError(ret);
            return ret >= 1;
        }

        // Check if at least a socket in the pool is ready for reading
        bool BerkeleyPool::isReadPossible(const Time::TimeOut & timeout)  const { return select(true, false, timeout); }
        // Check if at least a socket in the pool is ready for writing
        bool BerkeleyPool::isWritePossible(const Time::TimeOut & timeout) const { return select(false, true, timeout); }
        // Check if a socket is connected.
        bool BerkeleyPool::isConnected(const Time::TimeOut & timeout)
        {
            for (uint32 i = 0; i < size; i++) pool[i]->setOption(BaseSocket::Blocking, 0);
            bool ret = isReadPossible(timeout);
            for (uint32 j = 0; j < size; j++) pool[j]->setOption(BaseSocket::Blocking, 1);
            return ret;
        }

        // Check which socket was ready in the given pool
        int BerkeleyPool::getNextReadySocket(const int index) const
        {
            for (uint32 i = index+1; i < size; i++)
            {
                if (FD_ISSET(pool[i]->descriptor, &rset) || FD_ISSET(pool[i]->descriptor, &wset))
                    return (int)i;
            }
            return -1;
        }
        // Get the socket at the given position
        BaseSocket * BerkeleyPool::operator[] (const int index) { return index >= 0 && index < (int)size ?  pool[index] : 0; }
        // Get the socket at the given position
        const BaseSocket * BerkeleyPool::operator[] (const int index) const { return index >= 0 && index < (int)size ?  pool[index] : 0; }
        // Get the socket at the given position
        BaseSocket * BerkeleyPool::getReadyAt(const int index, bool * writing)  { if ((uint32)index >= size) return 0; if (writing) *writing = (bool)FD_ISSET(pool[index]->descriptor, &wset); return pool[index]; }
        // Check if we already have the given socket in the pool
        bool BerkeleyPool::haveSocket(BaseSocket * socket) const
        {
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket) return true;
            }
            return false;
        }
        // Get the index of the given socket in the pool
        uint32 BerkeleyPool::indexOf(BaseSocket * socket) const
        {
            for (uint32 i = 0; i < size; i++)
                if (pool[i] == socket) return i;
            return size;
        }

        // Select our for reading for reading, and the other pool for writing.
        int BerkeleyPool::selectMultiple(MonitoringPool * _other, const Time::TimeOut & timeout) const
        {
            if (!_other || (!size && !_other->getSize()) || _other->getTypeID() != 1) return 0;
            if (timeout <= 0) return 0; // Already timed out previously

            BerkeleyPool * other = (BerkeleyPool*)(_other);

            // Need to clear the sets, and start
            FD_ZERO(&rset); FD_ZERO(&other->wset);
            for (uint32 i = 0; i < size; i++) FD_SET(pool[i]->descriptor, &rset);
            for (uint32 j = 0; j < other->size; j++) FD_SET(other->pool[j]->descriptor, &other->wset);

            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;

            int ret = 0;
            // Then select
            while ((ret = ::select(FD_SETSIZE, &rset, &other->wset, NULL, timeout < 0 ? NULL : &tv)) == -1)
            {
#ifdef _WIN32
                if (WSAGetLastError() != WSAEINTR) { timeout.failed(-WSAGetLastError()); return 0; }
#else
                if (errno != EINTR) { timeout.failed(-errno); return 0; }
#endif
                if (timeout.timedOut()) return 0;
            }
            timeout.filterError(ret);
            if (ret >= 1)
            {   // Scan bitmaps for readyness
                for (uint32 i = 0; i < size; i++) if (FD_ISSET(pool[i]->descriptor, &rset)) { ret = 1; break; }
                for (uint32 i = 0; i < other->size; i++) if (FD_ISSET(other->pool[i]->descriptor, &other->wset)) { ret |= 2; break; }
            }
            return ret > 0 ? ret : 0;
        }
        // Clear the pool from all its sockets
        void BerkeleyPool::clearPool()
        {
            if (own)
            {
                // Destruct all the owned sockets
                for (uint32 i = 0; i < size; i++) { delete pool[i]; pool[i] = 0; }
            }
            size = 0;
            Platform::safeRealloc(pool, 0);
            pool = 0;
        }
        BerkeleyPool::~BerkeleyPool()
        {
            clearPool();
        }


#if defined(_POSIX)
        // Get the pool size
        uint32 FastBerkeleyPool::getSize() const { return size; }
        // Check if at least a socket in the pool is ready for reading
        bool FastBerkeleyPool::isReadPossible(const Time::TimeOut & timeout)  const { return select(true, false, timeout); }
        // Check if at least a socket in the pool is ready for writing
        bool FastBerkeleyPool::isWritePossible(const Time::TimeOut & timeout) const { return select(false, true, timeout); }
        // Check if a socket is connected.
        bool FastBerkeleyPool::isConnected(const Time::TimeOut & timeout)
        {
            for (uint32 i = 0; i < size; i++) pool[i]->setOption(BaseSocket::Blocking, 0);
            bool ret = isReadPossible(timeout);
            for (uint32 j = 0; j < size; j++) pool[j]->setOption(BaseSocket::Blocking, 1);
            return ret;
        }

        // Check which socket was ready in the given pool
        int FastBerkeleyPool::getNextReadySocket(const int index) const
        {
            return (index + 1 < triggerCount) && (uint32)(index + 1) < size  ? index + 1 : -1;
        }
        // Get the socket at the given position
        BaseSocket * FastBerkeleyPool::operator[] (const int index) { return index >= 0 && index < (int)size ?  pool[index] : 0; }
        // Get the socket at the given position
        const BaseSocket * FastBerkeleyPool::operator[] (const int index) const { return index >= 0 && index < (int)size ?  pool[index] : 0; }

        // Check if we already have the given socket in the pool
        bool FastBerkeleyPool::haveSocket(BaseSocket * socket) const
        {
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket) return true;
            }
            return false;
        }
        // Get the index of the given socket in the pool
        uint32 FastBerkeleyPool::indexOf(BaseSocket * socket) const
        {
            for (uint32 i = 0; i < size; i++)
                if (pool[i] == socket) return i;
            return size;
        }


        // Clear the pool from all its sockets
        void FastBerkeleyPool::clearPool()
        {
            if (own)
            {
                // Destruct all the owned sockets
                for (uint32 i = 0; i < size; i++) { delete pool[i]; pool[i] = 0; }
            }
            size = 0;
            Platform::safeRealloc(pool, 0);
            Platform::safeRealloc(events, 0);
            if (rd >= 0) close(rd); rd = -1;
            if (wd >= 0) close(wd); wd = -1;
            if (bd >= 0) close(bd); bd = -1;
            triggerCount = 0;
            events = 0;
            pool = 0;
        }

        FastBerkeleyPool::~FastBerkeleyPool()
        {
            clearPool();
        }

#ifdef _LINUX
        // Append a socket to this pool
        bool FastBerkeleyPool::appendSocket(BaseSocket * _socket)
        {
            if (!_socket || _socket->getTypeID() != 1) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);
            if (size >= MaxQueueLen) return false;

            if (rd < 0) rd = epoll_create(MaxQueueLen);
            if (wd < 0) wd = epoll_create(MaxQueueLen);
            if (bd < 0) bd = epoll_create(MaxQueueLen);

            if (rd < 0 || wd < 0 || bd < 0) return false;

            struct epoll_event ev = {0};
            ev.events = EPOLLIN;
            ev.data.ptr = socket;
            if (epoll_ctl(rd, EPOLL_CTL_ADD, socket->descriptor, &ev) != 0) return false;
            ev.events = EPOLLIN | EPOLLOUT;
            if (epoll_ctl(bd, EPOLL_CTL_ADD, socket->descriptor, &ev) != 0)
            {
                epoll_ctl(rd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                return false;
            }
            ev.events = EPOLLOUT;
            if (epoll_ctl(wd, EPOLL_CTL_ADD, socket->descriptor, &ev) != 0)
            {
                epoll_ctl(rd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                epoll_ctl(bd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                return false;
            }


            // Size has not been already incremented : will be only if all previous operations will succeed
            struct epoll_event * _events = (struct epoll_event *)Platform::safeRealloc(events, (size + 1) * sizeof(*_events));
            if (_events == NULL)
            {
                epoll_ctl(rd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                epoll_ctl(bd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                epoll_ctl(wd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                return false;
            }
            events = _events;

            BerkeleySocket ** newPool = (BerkeleySocket **)Platform::safeRealloc(pool, ( size + 1) * sizeof(*pool));
            if (newPool == NULL)
            {
                epoll_ctl(rd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                epoll_ctl(bd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                epoll_ctl(wd, EPOLL_CTL_DEL, socket->descriptor, &ev);

                Platform::safeRealloc(events, size * sizeof(*_events));
                return false;
            }
            pool = newPool;
            pool[size++] = socket;
            return true;
        }
        // Remove a socket from the pool
        bool FastBerkeleyPool::forgetSocket(BaseSocket * _socket)
        {
            if (!_socket && _socket->getTypeID() != 1) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);
            // Need to find out the socket in the pool
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket)
                {   // Found
                    triggerCount = 0; // If removed while iterating for events, let's redo selecting
                    // Swap the value with the last on in the list
                    pool[i] = pool[size - 1];
                    size--;
                    pool[size] = 0;

                    // Remove from the monitored element (we destruct the polling array to have good indexes)
                    struct epoll_event ev;
                    ev.events = 0;
                    ev.data.ptr = socket;
                    epoll_ctl(rd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                    epoll_ctl(wd, EPOLL_CTL_DEL, socket->descriptor, &ev);
                    epoll_ctl(bd, EPOLL_CTL_DEL, socket->descriptor, &ev);

                    events = (struct epoll_event *)Platform::safeRealloc(events, size * sizeof(struct epoll_event));
                    if (size && events == NULL) return false;

                    // We don't realloc smaller anyway
                    return true;
                }
            }
            return false;
        }
        // Remove a socket from the pool
        bool FastBerkeleyPool::removeSocket(BaseSocket * _socket)
        {
            if (!forgetSocket(_socket)) return false;
            if (own) delete (BerkeleySocket*)_socket;
            return true;
        }

        // Select the pool for at least an element that is ready
        bool FastBerkeleyPool::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
        {
            if (timeout < 0) return false; // Already timed out previously

            // wait for something to do...
            int fd = reading ? (writing ? bd : rd) : (writing ? wd : -1);
            if (fd < 0)
            {
                if (timeout <= 0) return timeout == 0;
                struct timespec ts;
                ts.tv_sec = timeout / 1000;
                ts.tv_nsec = (timeout % 1000) * 1000000;

                while (nanosleep(&ts, &ts) == -1);
                return true;
            }

            // Ok, now poll the pool
            triggerCount = epoll_wait(fd, (struct epoll_event*)events, size, (int)timeout < 0 ? -1 : (int)timeout);
            timeout.filterError(triggerCount);
            return triggerCount > 0;
        }
        // Select our for reading, and the other pool for writing.
        int FastBerkeleyPool::selectMultiple(MonitoringPool * _other, const Time::TimeOut & timeout) const
        {
            if (!_other) return select(true, false, timeout) == 1;
            if (timeout <= 0) return 0; // Already timed out previously
            if ((!size && !_other->getSize()) || _other->getTypeID() != getTypeID()) return 0; // Can not mix different pool (this should never happen in reality)
            // Under linux, we can have a epoll FD waiting in another epoll FD
            FastBerkeleyPool * other = (FastBerkeleyPool*)_other;
            Platform::FileIndexWrapper cd(epoll_create(2));
            if (cd < 0) return 0;
            struct epoll_event ev = {0}, _events[2] = {0};
            ev.events = EPOLLIN;
            ev.data.fd = rd;
            if (rd >= 0 && epoll_ctl(cd, EPOLL_CTL_ADD, rd, &ev) != 0) return 0;
            ev.events = EPOLLIN; // We are interested in "ready" event for the "write" descriptor
            ev.data.fd = other->wd;
            if (other->wd >= 0 && epoll_ctl(cd, EPOLL_CTL_ADD, other->wd, &ev) != 0) return 0; // There's no point in checking empty set

            int triggered = epoll_wait(cd, _events, 2, (int)timeout < 0 ? -1 : (int)timeout);
            timeout.filterError(triggerCount);
            if (triggered == 0) return 0;
            int ret = 0;
            if (triggered != 1 || _events[0].data.fd == rd) { select(true, false, timeout); ret = 1; }
            if (triggered != 1 || _events[0].data.fd == other->wd) { other->select(false, true, timeout); ret |= 2; }
            return ret;
        }

        // Get the ready socket at the given position
        BaseSocket * FastBerkeleyPool::getReadyAt(const int index, bool * writing)
        {
            struct epoll_event* _events = (struct epoll_event*)events;
            if ((uint32)index >= (uint32)triggerCount || (uint32)index >= size) return 0;
            if (writing) *writing = (_events[index].events & EPOLLOUT) > 0;
            return (BaseSocket*)_events[index].data.ptr;
        }

#elif defined(_MAC)
        // Append a socket to this pool
        bool FastBerkeleyPool::appendSocket(BaseSocket * _socket)
        {
            if (!_socket || _socket->getTypeID() != 1) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);
            if (size >= MaxQueueLen) return false;

            if (rd < 0) rd = kqueue();
            if (wd < 0) wd = kqueue();
            if (bd < 0) bd = kqueue();

            if (rd < 0 || wd < 0 || bd < 0) return false;

            struct kevent ev = {0};
            ev.ident = socket->descriptor;
            ev.flags = EV_ADD | EV_CLEAR;
            ev.filter = EVFILT_READ;
            ev.fflags = 0;
            ev.data = 0;
            ev.udata = socket;

            if (kevent(rd, &ev, 1, 0, 0, 0) < 0) return false;
            if (kevent(bd, &ev, 1, 0, 0, 0) < 0)
            {
                ev.filter = EVFILT_READ;
                ev.flags = EV_DELETE;
                kevent(rd, &ev, 1, 0, 0, 0);
                return false;
            }
            ev.filter = EVFILT_WRITE;
            if (kevent(wd, &ev, 1, 0, 0, 0) < 0)
            {
                ev.filter = EVFILT_READ;
                ev.flags = EV_DELETE;
                kevent(rd, &ev, 1, 0, 0, 0);
                ev.filter = EVFILT_READ;
                kevent(bd, &ev, 1, 0, 0, 0);
                return false;
            }
            if (kevent(bd, &ev, 1, 0, 0, 0) < 0)
            {
                ev.filter = EVFILT_READ;
                ev.flags = EV_DELETE;
                kevent(rd, &ev, 1, 0, 0, 0);
                kevent(bd, &ev, 1, 0, 0, 0);
                ev.filter = EVFILT_WRITE;
                kevent(wd, &ev, 1, 0, 0, 0);
                return false;
            }

            // Prepare for deletion on error
            ev.flags = EV_DELETE;

            // Size has not been already incremented : will be only if all previous operations will succeed
            struct kevent * _events = (struct kevent *)Platform::safeRealloc(events, (size + 1) * sizeof(*_events));
            if (_events == NULL)
            {
                ev.filter = EVFILT_READ;
                kevent(rd, &ev, 1, 0, 0, 0);
                kevent(bd, &ev, 1, 0, 0, 0);
                ev.filter = EVFILT_WRITE;
                kevent(wd, &ev, 1, 0, 0, 0);
                kevent(bd, &ev, 1, 0, 0, 0);
                return false;
            }
            events = _events;

            BerkeleySocket ** newPool = (BerkeleySocket **)Platform::safeRealloc(pool, ( size + 1) * sizeof(*pool));
            if (newPool == NULL)
            {
                ev.filter = EVFILT_READ;
                kevent(rd, &ev, 1, 0, 0, 0);
                kevent(bd, &ev, 1, 0, 0, 0);
                ev.filter = EVFILT_WRITE;
                kevent(wd, &ev, 1, 0, 0, 0);
                kevent(bd, &ev, 1, 0, 0, 0);

                Platform::safeRealloc(events, size * sizeof(*_events));
                return false;
            }
            pool = newPool;
            pool[size++] = socket;
            return true;
        }
        // Forget a socket from the pool
        bool FastBerkeleyPool::forgetSocket(BaseSocket * _socket)
        {
            if (!_socket || _socket->getTypeID() != 1) return false;
            BerkeleySocket * socket = (BerkeleySocket*)(_socket);
            // Need to find out the socket in the pool
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket)
                {   // Found
                    triggerCount = 0; // If removed while iterating for events, let's redo selecting
                    // Swap the value with the last on in the list
                    pool[i] = pool[size - 1];
                    size--;
                    pool[size] = 0;

                    // Remove from the monitored element (we destruct the polling array to have good indexes)
                    struct kevent ev = {0};
                    ev.ident = socket->descriptor;
                    ev.flags = EV_DELETE;
                    ev.filter = EVFILT_READ;
                    ev.fflags = 0;
                    ev.data = 0;
                    ev.udata = socket;

                    kevent(rd, &ev, 1, 0, 0, 0);
                    kevent(bd, &ev, 1, 0, 0, 0);
                    ev.filter = EVFILT_WRITE;
                    kevent(wd, &ev, 1, 0, 0, 0);
                    kevent(bd, &ev, 1, 0, 0, 0);

                    events = (struct kevent *)Platform::safeRealloc(events, size * sizeof(struct kevent));
                    if (size && events == NULL) return false;

                    // We don't realloc smaller anyway
                    return true;
                }
            }
            return false;
        }
        // Remove a socket from the pool
        bool FastBerkeleyPool::removeSocket(BaseSocket * _socket)
        {
            if (!forgetSocket(_socket)) return false;
            if (own) delete (BerkeleySocket*)_socket;
            return true;
        }
        // Select the pool for at least an element that is ready
        bool FastBerkeleyPool::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
        {
            if (timeout < 0) return false; // Already timed out previously

            // wait for something to do...
            int fd = reading ? (writing ? bd : rd) : (writing ? wd : -1);
            if (fd < 0)
            {
                if (timeout <= 0) return timeout == 0;
                struct timespec ts;
                ts.tv_sec = timeout / 1000;
                ts.tv_nsec = (timeout % 1000) * 1000000;

                while (nanosleep(&ts, &ts) == -1);
                return true;
            }

            // Ok, now poll the pool
            struct timespec timeoutTV = {0}; timeoutTV.tv_sec = timeout / 1000; timeoutTV.tv_nsec = (timeout % 1000) * 1000 * 1000;
            triggerCount = kevent(fd, 0, 0, (struct kevent*)events, size, timeout < 0 ? NULL : &timeoutTV);
            timeout.filterError(triggerCount);
            return triggerCount > 0;
        }
        // Select our for reading, and the other pool for writing.
        int FastBerkeleyPool::selectMultiple(MonitoringPool * _other, const Time::TimeOut & timeout) const
        {
            if (!_other) return select(true, false, timeout) == 1;
            if (timeout <= 0) return 0; // Already timed out
            if ((!size && !_other->getSize()) || _other->getTypeID() != getTypeID()) return 0; // Can not mix different pool (this should never happen in reality)
            // Under linux, we can have a epoll FD waiting in another epoll FD
            FastBerkeleyPool * other = (FastBerkeleyPool*)_other;
            Platform::FileIndexWrapper cd(kqueue());
            if (cd < 0) return 0;
            struct kevent ev = {0}, _events[2] = {{0},{0}};
            ev.ident = rd; ev.flags = EV_ADD | EV_CLEAR; ev.filter = EVFILT_READ;
            if (rd >= 0 && kevent(cd, &ev, 1, 0, 0, 0) < 0) return 0;

            ev.ident = other->wd;
            ev.filter = EVFILT_READ;
            if (other->wd >= 0 && kevent(cd, &ev, 1, 0, 0, 0) < 0) return 0;

            struct timespec timeoutTV = {0}; timeoutTV.tv_sec = timeout / 1000; timeoutTV.tv_nsec = (timeout % 1000) * 1000 * 1000;
            int triggered = kevent(cd, 0, 0, _events, 2, (int)timeout < 0 ? NULL : &timeoutTV);
            timeout.filterError(triggered);
            if (triggered == 0) return 0;
            int ret = 0;
            if (triggered != 1 || _events[0].ident == rd)  { select(true, false, timeout); ret = 1; } // Timeout here is useless, but avoid creating another object here for nothing
            if (triggered != 1 || _events[0].ident == other->wd) { other->select(false, true, timeout); ret |= 2; }
            return ret;
        }

        // Get the ready socket at the given position
        BaseSocket * FastBerkeleyPool::getReadyAt(const int index, bool * writing)
        {
            struct kevent* _events = (struct kevent*)events;
            if ((uint32)index >= triggerCount || (uint32)index >= size) return 0;
            if (writing) *writing = (_events[index].filter & EVFILT_WRITE) > 0 && _events[index].data > 0;
            return (BaseSocket*)_events[index].udata;
        }

#endif
#endif





#if DEBUG == 1
        struct VerbLog
        {
            const char * oper;
            const BerkeleySocket & socket;

            String addrTxt(const Address * addr) { if (addr) { String ret = addr->identify(); delete addr; return ret; } return "<not bound>"; }
            const char * fromErr(BaseSocket::Error ret)
            {
                switch(ret)
                {
                case BaseSocket::Success:         return "Success";
                case BaseSocket::InProgress:      return "InProgress";
                case BaseSocket::Refused:         return "Refused";
                case BaseSocket::StillInUse:      return "StillInUse";
                case BaseSocket::BadCertificate:  return "BadCertificate";
                case BaseSocket::OtherError:      return "OtherError";
                case BaseSocket::BadArgument:     return "BadArgument";
                }
                return "Unknown value";
            }

            void dump(const char * buf, size_t sz, int flags) { VerbLogMed(Logger::Dump|Logger::Packet, "Buffer (%uB): \"%s\" fl%d", (uint32)sz, (const char*)Utils::hexDump(buf, (uint32)sz), flags); }
            void dump(const char ** buffers, const int * buffersSize, const int buffersCount, const int flags)
            {
                for (int i = 0; i < buffersCount; i++)
                   VerbLogMed(Logger::Dump|Logger::Packet, "Buffer %d (%uB): \"%s\" fl%d", i, (uint32)buffersSize[i], (const char*)Utils::hexDump(buffers[i], (uint32)buffersSize[i]), flags);
            }
            void dump(const char * buf, size_t sz, const Network::Address::BaseAddress & to) { VerbLogMed(Logger::Dump|Logger::Packet, "Buffer (%uB): \"%s\" to %s", (uint32)sz, (const char*)Utils::hexDump(buf, (uint32)sz), (const char*)to.identify()); }
            void dump(const char ** buffers, const int * buffersSize, const int buffersCount, const Network::Address::BaseAddress & to)
            {
                for (int i = 0; i < buffersCount; i++)
                   VerbLogMed(Logger::Dump|Logger::Packet, "Buffer %d (%uB): \"%s\" to %s", i, (uint32)buffersSize[i], (const char*)Utils::hexDump(buffers[i], (uint32)buffersSize[i]), (const char*)to.identify());
            }

            int ReturnBuf(int len, char * buffer, const int bufferSize, Address * from) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned: %d from %s with buffer (%uB) \"%s\"", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), len, from ? (const char*)from->identify() : "<unknown>", bufferSize, len > 0 ? (const char*)Utils::hexDump(buffer, (uint32)len) : "<N/A>"); return len; }
            int ReturnBuf(int len, char * buffer, const int bufferSize, int fl) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned: %d fl %d with buffer (%uB) \"%s\"", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), len, fl, bufferSize, len > 0 ? (const char*)Utils::hexDump(buffer, (uint32)len) : "<N/A>"); return len; }


            int Return(int obj, const char *) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned: %d", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), obj); return obj; }
            bool ReturnI(bool ret, int obj, const char * opt) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned: %s (with value %s = %d)", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), ret ? "true" : "false", opt, obj); return ret; }
            BaseSocket * Return(BaseSocket * ret, const char *) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned socket %p", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), ret); return ret; }
            BaseSocket::Error Return(BaseSocket::Error ret, const char * ) { VerbLogMed(Logger::Dump|Logger::Packet, "[%.3f] %s on socket %s returned: %s", Time::Time::Now().preciseTime(), oper, (const char*)addrTxt(socket.getBoundAddress()), fromErr(ret)); return ret; }
            VerbLog(const char * oper, const BerkeleySocket & socket) : oper(oper), socket(socket) {}

        };
  #define CaptureCall    VerbLog __ret__(__FUNCTION__, *this)
  #define Return(X)      return __ret__.Return(X, #X)
  #define ReturnI(X, N, V)      return __ret__.ReturnI(X, V, #N)
  #define ReturnBuf(X, B,S,F)      return __ret__.ReturnBuf(X, B,S,F)
  #define DumpBuffer(B, S, F) __ret__.dump(B,S,F)
  #define DumpBuffers(B,BS,S,F) __ret__.dump(B,BS,S,F)

#else
  #define CaptureCall
  #define Return(X)      return X
  #define ReturnI(X, N, V)     return X
  #define ReturnBuf(X, B,S,F)  return X
  #define DumpBuffer(B, S, F)
  #define DumpBuffers(B,BS,S,F)
#endif





        // Open a socket of the specified type
        bool BerkeleySocket::open(const BerkeleySocket::Type _type)
        {
            // We delay opening the socket until it's first used
            type = _type;
            return true;
        }
        // Open a socket of the specified type
        bool BerkeleySocket::open(const bool ipv6)
        {
            CaptureCall;
            // Already open ?
            if (descriptor != SocketError) Return(false);
            switch(type)
            {
            case Datagram:
                descriptor = ::socket(!ipv6 ? PF_INET : PF_INET6, SOCK_DGRAM, 0);
                break;
            case Stream:
                descriptor = ::socket(!ipv6 ? PF_INET : PF_INET6, SOCK_STREAM, 0);
                break;
            case Raw:
                descriptor = ::socket(!ipv6 ? PF_INET : PF_INET6, SOCK_RAW, 0);
                break;
            case ICMP:
                descriptor = ::socket(!ipv6 ? PF_INET : PF_INET6, SOCK_RAW, 0);
#ifdef _WIN32
                if (descriptor != SocketError)
                {
                    DWORD hdrInc = 0;
                    ::setsockopt(descriptor, !ipv6 ? AF_INET : AF_INET6, !ipv6 ? IP_HDRINCL : IPV6_HDRINCL, (char*)&hdrInc, sizeof(hdrInc));
                }
#endif
                break;
            default:
                descriptor = SocketError;
                break;
            }
            setError((int)descriptor);
            if (descriptor != SocketError) state = Opened;
            Return(descriptor != SocketError);
        }
        // Close the socket
        bool BerkeleySocket::close()
        {
            CaptureCall;

            if (descriptor != SocketError)
#ifdef _WIN32
                ::closesocket(descriptor);
#else
                ::close(descriptor);
#endif
            descriptor = SocketError;
            state = Unset;
            lastError = Success;
            Return(true);
        }
        // Accept a connection (and fill the address on success)
        Connection * BerkeleySocket::getConnection()
        {
            if (type == Datagram)
                return new Network::Connection::Datagram(new Local(*this), getPeerName());
            else return new Network::Connection::Stream(new Local(*this), getPeerName());
        }
        // Accept a connection (and fill the address on success)
        BaseSocket * BerkeleySocket::accept(Address *& address)
        {
            CaptureCall;
            if (descriptor == SocketError)
            {   // We can't accept without any socket
                Return((BaseSocket*)0);
            }

            // Then, accept on our descriptor
            struct sockaddr_in6 bigaddr = {0}; socklen_t socklen = sizeof(bigaddr);
            sock_t desc = ::accept(descriptor, (struct sockaddr*)&bigaddr, &socklen);
            if (desc == SocketError) Return((BaseSocket*)0);

            if (socklen != sizeof(bigaddr))
            {   // IPv4
                struct sockaddr_in * addr = (struct sockaddr_in*)&bigaddr;
                if (sizeof(*addr) == socklen)
                {
                    Network::Address::IPV4 * realAddress = (Network::Address::IPV4 *)address;
                    if (realAddress && address->getTypeID() == 1)
                        *realAddress = Network::Address::IPV4((uint32)ntohl(addr->sin_addr.s_addr), (uint16)ntohs(addr->sin_port));
                    else
                    {
                        delete address;
                        address = new Network::Address::IPV4((uint32)ntohl(addr->sin_addr.s_addr), (uint16)ntohs(addr->sin_port));
                    }
                }
                Return(new BerkeleySocket(desc, type, Connected));
            } else
            {   // IPV6
                struct sockaddr_in6 *addr = (struct sockaddr_in6*)&bigaddr;
                if (sizeof(*addr) == socklen)
                {
                    Network::Address::IPV6 * realAddress = dynamic_cast<Network::Address::IPV6 *>(address);
                    if (realAddress && address->getTypeID() == 2)
                        *realAddress = Network::Address::IPV6((const uint8 *)addr->sin6_addr.s6_addr, (uint16)ntohs(addr->sin6_port));
                    else
                    {
                        if (address) delete address;
                        address = new Network::Address::IPV6((const uint8 *)addr->sin6_addr.s6_addr, (uint16)ntohs(addr->sin6_port));
                    }
                }
                Return(new BerkeleySocket(desc, type, Connected));
            }
        }

        // Set the state and return success
        BerkeleySocket::Error BerkeleySocket::setState(const BerkeleySocket::State newState)
        {
            state = newState;
            return Success;
        }

        // Bind a socket on the given address
        BerkeleySocket::Error BerkeleySocket::bind(const Address & _address, const bool allowBroadcast)
        {
            CaptureCall;
            // Ensure we are using the low level stuff now
            const Address & address = _address.getBindableAddress();
            const Network::Address::IPV6 * ipv6 = address.getTypeID() == 2 ? (const Network::Address::IPV6 *)(&address) : 0;
            if (ipv6)
            {
                // If not open yet, open now
                if (descriptor == SocketError && !open(true)) Return(OtherError);
                struct sockaddr_in6 addr;
                memcpy(&addr, ipv6->getLowLevelObject(), sizeof(addr));
                // If the port is set, but not the address, then make sure the socket is bound on all interfaces
                if (!allowBroadcast && !ipv6->isValid() && addr.sin6_port) memset(&addr.sin6_addr.s6_addr, 0, sizeof(addr.sin6_addr));
                // Then really bind that stuff
                Return(setError(::bind(descriptor, (struct sockaddr*)&addr, sizeof(addr))) == 0 ? setState(Bound) : getLastError());
            } else
            {
                if (descriptor == SocketError && !open(false)) Return(OtherError);
                // Then really bind that stuff
                struct sockaddr_in addr;
                memcpy(&addr, address.getLowLevelObject(), sizeof(addr));
                // If the port is set, but not the address, then make sure the socket is bound on all interfaces
                if (!allowBroadcast && !address.isValid() && addr.sin_port) memset(&addr.sin_addr.s_addr, 0, sizeof(addr.sin_addr));
                Return(setError(::bind(descriptor, (struct sockaddr*)&addr, sizeof(addr))) == 0 ? setState(Bound) : getLastError());
            }
        }

        // Bind on a multicast group.
        BerkeleySocket::Error BerkeleySocket::bindOnMulticast(const Address & multicastAddress, const Address * interfaceAddress)
        {
            CaptureCall;
            if (!multicastAddress.isValid()) Return(BadArgument);
            if (multicastAddress.getTypeID() == 1)
            {   // IPv4
                struct ip_mreq ipAddr;
                ipAddr.imr_multiaddr = ((struct sockaddr_in*)multicastAddress.getLowLevelObject())->sin_addr;
                ipAddr.imr_interface.s_addr = INADDR_ANY;

                if (!multicastAddress.getPort())
                    // Need to drop the membership
                    Return(setError(::setsockopt(descriptor, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const char*)&ipAddr, sizeof(ipAddr))) == 0 ? Success : getLastError());
                else
                {
                    Network::Address::IPV4 local((uint32)0, multicastAddress.getPort());
                    const Address & localAddress = interfaceAddress ? interfaceAddress->getBindableAddress() : local;
                    Error error = bind(localAddress, false);
                    if (error != Success) Return(error);
                    // Then add the membership
                    Return(setError(::setsockopt(descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&ipAddr, sizeof(ipAddr))) == 0 ? Success : getLastError());
                }
            }
            else
            {
                struct ipv6_mreq ipAddr;
                ipAddr.ipv6mr_multiaddr = ((struct sockaddr_in6*)multicastAddress.getLowLevelObject())->sin6_addr;
                memset(&ipAddr.ipv6mr_multiaddr, 0, sizeof(ipAddr.ipv6mr_interface)); // Actually it's 0
                if (!multicastAddress.getPort())
                    // Need to drop the membership
                    Return(setError(::setsockopt(descriptor, IPPROTO_IPV6, IPV6_LEAVE_GROUP, (const char*)&ipAddr, sizeof(ipAddr))) == 0 ? Success : getLastError());
                else
                {
                    const Address & localAddress = interfaceAddress ? interfaceAddress->getBindableAddress() : (const Address &)Network::Address::IPV6(0, 0, 0, 0, 0, 0, 0, 0, multicastAddress.getPort());
                    Error error = bind(localAddress, false);
                    if (error != Success) Return(error);
                    // Then add the membership
                    Return(setError(::setsockopt(descriptor, IPPROTO_IPV6, IPV6_JOIN_GROUP, (const char*)&ipAddr, sizeof(ipAddr))) == 0 ? Success : getLastError());
                }
            }
        }


        // Connect a socket to the given address
        BerkeleySocket::Error BerkeleySocket::connect(const Address & _address)
        {
            CaptureCall;
            const Address & address = _address.getBindableAddress();
            const Network::Address::IPV6 * ipv6 = address.getTypeID() == 2 ? (const Network::Address::IPV6 *)(&address) : 0;
            if (ipv6)
            {
                // If not open yet, open now
                if (descriptor == SocketError && !open(true)) Return(OtherError);
                // Then really bind that stuff
                if (setError(::connect(descriptor, (struct sockaddr*)ipv6->getLowLevelObject(), sizeof(sockaddr_in6))) == 0)
                    Return(setState(Connected) == Success ? connectionDone() : InProgress);
            } else
            {
                if (descriptor == SocketError && !open(false)) Return(OtherError);
                // Then really bind that stuff
                if (setError(::connect(descriptor, (struct sockaddr*)address.getLowLevelObject(), sizeof(sockaddr_in))) == 0)
                    Return(setState(Connected) == Success ? connectionDone() : InProgress);
            }
            Return(getLastError() == InProgress ? setState(Connecting) : getLastError());
        }
        // Listen on such socket
        BerkeleySocket::Error BerkeleySocket::listen(const int maxAllowedConnection)
        {
            CaptureCall;
            // If not open yet, open now
            if (descriptor == SocketError && !open(false)) Return(OtherError);
            // Then really bind that stuff
            Return(setError(::listen(descriptor, maxAllowedConnection < 0 ? SOMAXCONN : maxAllowedConnection)) == 0 ? setState(Listening) : getLastError());
        }

        // Receive data on the socket
        int BerkeleySocket::receive(char * buffer, const int bufferSize, const int flags) const
        {
            CaptureCall;
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
            ReturnBuf(setError(::recv(descriptor, buffer, bufferSize, flags)), buffer, bufferSize, flags);
        }
        // Receive data on the datagram socket
        int BerkeleySocket::receiveFrom(char * buffer, const int bufferSize, Address *& from) const
        {
            CaptureCall;
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
            struct sockaddr_in6 addr; socklen_t addrlen = sizeof(addr);
            int ret = setError(::recvfrom(descriptor, buffer, bufferSize, 0, (struct sockaddr*)&addr, &addrlen));

            delete from;
            if (addrlen == sizeof(struct sockaddr_in))
            {   // IPv4
                struct sockaddr_in * _addr = (struct sockaddr_in *)&addr;
                from = new Network::Address::IPV4((uint32)ntohl(_addr->sin_addr.s_addr), (uint16)ntohs(_addr->sin_port));
            } else
            {   // IPv6
                from = new Network::Address::IPV6((const uint8 *)addr.sin6_addr.s6_addr, ntohs(addr.sin6_port));
            }
            ReturnBuf(ret, buffer, bufferSize, from);
        }

        // Send data on the socket
        int BerkeleySocket::send(const char * buffer, const int bufferSize, int flags) const
        {
            CaptureCall;
            DumpBuffer(buffer, bufferSize, flags);
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
#ifdef _LINUX
            flags |= sendOptions;
#endif
            Return(setError(::send(descriptor, buffer, bufferSize, flags)));
        }

        // Send data on the datagram socket
        int BerkeleySocket::sendTo(const char * buffer, const int bufferSize, const Network::Address::BaseAddress & to) const
        {
            CaptureCall;
            DumpBuffer(buffer, bufferSize, to);
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
            int flags = 0;
#ifdef _LINUX
            flags = sendOptions;
#endif
            Return(setError(::sendto(descriptor, buffer, bufferSize, flags, (struct sockaddr*)to.getLowLevelObject(), to.getLowLevelObjectSize())));
        }

        // Send data from multiple buffer at once.
        int BerkeleySocket::sendBuffers(const char ** buffers, const int * buffersSize, const int buffersCount, const int flags) const
        {
            CaptureCall;
            DumpBuffers(buffers, buffersSize, buffersCount, flags);
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
            if (buffers == 0 || buffersSize == 0) Return(-1);
#ifdef _WIN32
            WSABUF * vectors = new WSABUF[buffersCount];
            for (int i = 0; i < buffersCount; i++)
            {
                vectors[i].buf = (char*)buffers[i];
                vectors[i].len = buffersSize[i];
            }
            DWORD sent = 0;
            int ret = setError(WSASend(descriptor, vectors, buffersCount, &sent, 0, NULL, NULL));
            delete[] vectors;
            Return(ret == 0 ? sent : ret);
#elif defined(_POSIX)
            typedef struct iovec iovec;
            iovec * vectors = new iovec[buffersCount];
            for (int i = 0; i < buffersCount; i++)
            {
                vectors[i].iov_base = (void*)buffers[i];
                vectors[i].iov_len = buffersSize[i];
            }
            int ret = setError(::writev(descriptor, vectors, buffersCount));
            delete[] vectors;
            Return(ret);
#else
            return BaseSocket::sendBuffers(buffers, buffersSize, buffersCount, flags);
#endif
        }

        // Send data from multiple buffer to an host.
        int BerkeleySocket::sendBuffersTo(const char ** buffers, const int * buffersSize, const int buffersCount, const Address & to) const
        {
            CaptureCall;
            DumpBuffers(buffers, buffersSize, buffersCount, to);
            if (descriptor == SocketError && !const_cast<BerkeleySocket*>(this)->open(false)) Return(-1);
            if (buffers == 0 || buffersSize == 0) Return(-1);
#ifdef _WIN32
            WSABUF * vectors = new WSABUF[buffersCount];
            for (int i = 0; i < buffersCount; i++)
            {
                vectors[i].buf = (char*)buffers[i];
                vectors[i].len = buffersSize[i];
            }
            DWORD sent = 0;
            int ret = setError(WSASendTo(descriptor, vectors, buffersCount, &sent, 0, (struct sockaddr*)to.getLowLevelObject(), to.getLowLevelObjectSize(),  NULL, NULL));
            delete[] vectors;
            Return(ret == 0 ? sent : ret);
#elif defined(_POSIX)
            typedef struct iovec iovec;
            iovec * vectors = new iovec[buffersCount];
            for (int i = 0; i < buffersCount; i++)
            {
                vectors[i].iov_base = (void*)buffers[i];
                vectors[i].iov_len = buffersSize[i];
            }
            struct msghdr msg; memset(&msg, 0, sizeof(msg));
            msg.msg_name = (void *)to.getLowLevelObject();
            msg.msg_namelen = to.getLowLevelObjectSize();
            msg.msg_iov = vectors;
            msg.msg_iovlen = buffersCount;
            int flags = 0;
#ifdef _LINUX
            flags = sendOptions;
#endif

            int ret = setError(::sendmsg(descriptor, &msg, flags));
            delete[] vectors;
            Return(ret);
#else
            return BaseSocket::sendBuffers(buffers, buffersSize, buffersCount, 0);
#endif
        }



        /** This thread waits for events on async stuff */
        struct WaitingThread : public Threading::Thread
        {
            /** The item structure */
            struct Item
            {
                BerkeleySocket           *      socket;
                BaseSocket::SDAFCallback *      callback;
                uint64                      sent;
#ifdef _WIN32
                OVERLAPPED                  over;
                TRANSMIT_PACKETS_ELEMENT    transmit[3];
                uint32                      used;
                uint64                      remaining;

#else
                uint8 *                     prefixBuffer;
                uint32                      prefixLength;
                int                         file;
                uint64                      size;
                uint64                      offset;
                uint8 *                     suffixBuffer;
                uint32                      suffixLength;
#endif


                Item(const char * prefixBuffer, const int prefixLength, const String & filePath, const uint64 offset, const uint64 size,
                                                const char * suffixBuffer, const int suffixLength, BaseSocket::SDAFCallback & callback, BerkeleySocket * sock)
                                                : socket(sock), callback(&callback), sent(0)
#ifndef _WIN32
                                                    , prefixBuffer((uint8*)prefixBuffer), prefixLength((uint32)prefixLength), file(-1), size(0), offset(0)
                                                    , suffixBuffer((uint8*)suffixBuffer), suffixLength((uint32)suffixLength)
#endif
                {
#ifdef _WIN32
                    memset(&over, 0, sizeof(over));
                    memset(transmit, 0, sizeof(transmit));
                    over.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                    used = 0; remaining = 0;
                    if (prefixBuffer && prefixLength)
                    {
                        transmit[0].cLength = prefixLength; transmit[0].pBuffer = (void*)prefixBuffer; transmit[0].dwElFlags = TP_ELEMENT_MEMORY;
                        remaining = prefixLength;
                        used++;
                    }
                    if (filePath.getLength())
                    {
                        Strings::ReadOnlyUnicodeString name = Strings::convert(filePath);
                        transmit[used].hFile = CreateFileW(name.getData(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                        if (transmit[used].hFile != INVALID_HANDLE_VALUE)
                        {
                            transmit[used].cLength = (ULONG)(size > 0xFFFFFFFF ? 0xFFFFFFFF : size);
                            transmit[used].dwElFlags = TP_ELEMENT_FILE;
                            if (size == 0)
                            {
                                DWORD high = 0, low = 0;
                                low = GetFileSize(transmit[used].hFile, &high);
                                remaining += (((uint64)high) << 32) | low;
                            }
                            else remaining += size;
                            transmit[used].nFileOffset.QuadPart = offset;
                            remaining -= offset;
                            used++;
                        }
                    }
                    if (suffixBuffer && suffixLength)
                    {
                        transmit[used].cLength = suffixLength; transmit[used].pBuffer = (void*)suffixBuffer; transmit[used].dwElFlags = TP_ELEMENT_MEMORY;
                        remaining += suffixLength;
                        used++;
                    }
#elif defined(_POSIX)
                    if (filePath.getLength())
                    {
                        this->file = open((const char*)filePath, O_RDONLY | O_NONBLOCK);
                        this->offset = offset;
                        this->size = size;
                        if (size == 0)
                        {   // Read the size from the file system
                            struct stat status;
                            if (fstat(this->file, &status) == 0)
                                this->size = status.st_size - offset;
                        }
                    }
#endif
                }

            };

            /** The callback to use */
            struct Callback
            {
                BerkeleySocket *                socket;
                BaseSocket::SDAFCallback *      callback;
                uint64                          sentSize;

                Callback(BerkeleySocket * _socket, BaseSocket::SDAFCallback * _callback, const uint64 _sentSize = (uint64)-1)
                    : socket(_socket), callback(_callback), sentSize(_sentSize) {}
                Callback() : socket(0), callback(0), sentSize(0)                {}
            };
        public:
            /** The items list : only used by the main loop of the waiting thread*/
            Container::NotConstructible<Item>::IndexList    workList;
            /** The items list that are waiting for to be moved into the work list and to be managed by the waiting thread*/
            Container::WithCopyConstructor<Item *>::Array   pendingList;
            /** The error list */
            Container::WithCopyConstructor<Callback>::Array callbackList;
            /** The Lock to protect pending list */
            volatile Threading::Lock                        lockPendingList;
            /** A manual event to notify an add in the pending list */
            volatile Threading::Event                       pendingListEvent;
            /** Auto reset event to notify a removed is asked */
            volatile Threading::Event                       removeRequired;
            /** Auto reset event to notify the main loop is waiting for the remove execution */
            volatile Threading::Event                       mainLoopWaiting;
            /** Auto reset event to notify the remove has been done */
            volatile Threading::Event                       removeDone;
            /** The Lock to concurrent removed */
            volatile Threading::Lock                        lockRemove;

#ifdef _POSIX
            /** The epoll/kqueue FD set for writing */
            int                     wd;
            /** The last request triggered event's count */
            mutable int             triggerCount;
#if defined(_LINUX)
            /** The events array to monitor */
            struct epoll_event *    events;
#endif
#if defined(_MAC)
            /** The events array to monitor */
            struct kevent * events;
#endif
#endif

#ifdef _POSIX
        private:
            /** Add the socket to the event pool */
            bool addSocketToEventPool(Item * item)
            {
                if (workList.getSize() > 256)
                    return false;

                item->socket->setOption(BaseSocket::Cork, 1);
                item->socket->setOption(BaseSocket::Blocking, 0);

#if defined(_LINUX)
                if (wd < 0) wd = epoll_create(256);
                if (wd < 0) return false;

                struct epoll_event ev = {0};
                ev.data.ptr = item;
                ev.events = EPOLLOUT;
                if (epoll_ctl(wd, EPOLL_CTL_ADD, item->socket->descriptor, &ev) != 0)
                    return false;

                struct epoll_event * _events = (struct epoll_event *)Platform::safeRealloc(events, workList.getSize() * sizeof(*_events));
                if (_events == NULL)
                {
                    epoll_ctl(wd, EPOLL_CTL_DEL, item->socket->descriptor, &ev);
                    return false;
                }
#else
                if (wd < 0) wd = kqueue();
                if (wd < 0) return false;

                struct kevent kev = {0};

                kev.ident = item->socket->descriptor;
                kev.flags = EV_ADD | EV_CLEAR;
                kev.filter = EVFILT_READ;
                kev.fflags = 0;
                kev.data = 0;
                kev.udata = item;

                if (kevent(wd, &kev, 1, 0, 0, 0) != 1)
                    return false;

                struct kevent * _events = (struct kevent *)Platform::safeRealloc(events, workList.getSize() * sizeof(*_events));
                if (_events == NULL)
                {
                    kev.flags = EV_DELETE;
                    kevent(wd, &kev, 1, 0, 0, 0);
                    return false;
                }
#endif
                events = _events;
                return true;
            }
#endif // _WIN32

            /** The socket remove from all buffer needs some common operations */
            bool movePendingListIntoWorkingList()
            {
                // Work list Lock has been taken
                Threading::ScopedLock scope(lockPendingList);
                size_t previousWorkListSize = workList.getSize();
                // Move the items from pendingList to workList
                for(uint32 i = 0; i < (uint32)pendingList.getSize(); i++)
                {
                    Item * currentItem = pendingList[i];
                    if(currentItem)
                        // Copy address and work list will delete the memory
                        workList.Append(currentItem);
                }
                // The pending list is clear
                pendingList.Clear();

#ifdef _POSIX
                // For the Linux version and only for the new item of the working list, some additional tasks need being done
                // Done after worklist filled because tell error use this list
                for(int i = (int)(workList.getSize() - 1);  i >=  (int)previousWorkListSize; i--)
                {
                    Item & currentItem = workList[i];
                    if(!addSocketToEventPool(&currentItem))
                    {
                        // need notifying error
                        uint32 count = 0;
                        tellError(currentItem, (uint32)-1, count);
                    }
                }
#endif
                return true;
            }

            /** The socket remove from all buffer needs some common operations */
            bool resetSocketOnRemove(Item & item)
            {
#ifdef _POSIX
#if defined(_LINUX)
                struct epoll_event ev = {0};
                epoll_ctl(wd, EPOLL_CTL_DEL, item.socket->descriptor, &ev);
#elif defined(_MAC)
                struct kevent kev = {0};
                kev.ident = item.socket->descriptor;
                kev.flags = EV_DELETE;
                kev.filter = EVFILT_READ;
                kevent(wd, &kev, 1, 0, 0, 0);
#endif
                item.socket->setOption(BaseSocket::Cork, 0);
                if(item.file >= 0) { close(item.file); item.file = -1; }
#endif
                // Tell the error now
                if (item.callback)
                    item.callback->finishedSending(item.socket, -1);
                return true;
            }
        public:

            /** Append data to the thread */
            bool appendItem(const char * prefixBuffer, const int prefixLength, const String & filePath, const uint64 offset, const uint64 size,
                                                const char * suffixBuffer, const int suffixLength, BaseSocket::SDAFCallback & callback, BerkeleySocket * sock)
            {
                bool result = true;
                Threading::ScopedLock scope(lockPendingList);
                Item * item = new Item(prefixBuffer, prefixLength, filePath, offset, size, suffixBuffer, suffixLength, callback, sock);
                pendingList.Append(item);
#ifdef _WIN32
                if(MyTransmitPackets(sock->descriptor, item->transmit, item->used, 0, &item->over, TF_USE_KERNEL_APC) != TRUE)
                {
                    pendingList.Remove(pendingList.getSize() - 1);
                    delete item;
                    return false;
                }
#endif
                if (!isRunning()) result = createThread();
                if (result)
                {
                    // wake up the main thread
                    pendingListEvent.Set();
                }
                return true;
            }

            /** Remove an existing item from the pool */
            bool removeItem(BerkeleySocket * sock)
            {
                // avoid concurrent remove
                Threading::ScopedLock scope(lockRemove);
                // Here we say to the main loop a remove is required
                removeRequired.Set();
                // We wait the main loop stop itself with no lock taken
                while (!mainLoopWaiting.Wait(20) && isRunning()) {}

                // Here the main loop is not running : no callback concurrent access can happen
                // If in the pending list
                {
                    Threading::ScopedLock scope(lockPendingList);
                    // Remove the item if it's in the pending list
                    // If in pending list not in working list
                    for(uint32 i = 0; i < (uint32)pendingList.getSize(); i++)
                    {
                        Item * currentItem = pendingList[i];
                        if(currentItem->socket == sock)
                        {
                            resetSocketOnRemove(*currentItem);
                            delete currentItem;
                            pendingList.Remove(i);
                            // Free the main loop
                            removeDone.Set();
                            return true;
                        }
                    }
                }
                // If in the working list
                for (uint32 i = 0; i < workList.getSize(); i++)
                {
                    Item & item = workList[i];
                    if (item.socket == sock)
                    {
                        // Found, let's remove it now
                        resetSocketOnRemove(item);
                        // The socket is removed from the list : no action will be done on the current socket
                        // specially the callback management
                        workList.Remove(i);
                        // Remove the item from the callback list
                        for (uint32 j = 0; j < callbackList.getSize(); j++)
                        {
                            if (callbackList.getElementAtUncheckedPosition(j).socket == sock)
                            { callbackList.Remove(j); break; }
                        }
                        // Free the main loop
                        removeDone.Set();
                        return true;
                    }
                }

                // Free the main loop
                removeDone.Set();
                return false;
            }

            /** Trigger the queued callback */
            void triggerCallbacks()
            {
                for (uint32 i = 0; i < (uint32)callbackList.getSize(); i++)
                {
                    Callback & cb = callbackList.getElementAtUncheckedPosition(i);
                    if (cb.callback && cb.socket)
                        cb.callback->finishedSending(cb.socket, cb.sentSize);
                }
                callbackList.Clear();
            }

            void tellError(Item & item, uint32 i, uint32 & count)
            {
                // No lock her fore the callbacks : The remove of the list managed the protections
                // Please see removeItem
                callbackList.Append(Callback(item.socket, item.callback));
#ifndef _POSIX
                workList.Remove(i);
#else
#if defined(_LINUX)
                struct epoll_event ev = {0};
                epoll_ctl(wd, EPOLL_CTL_DEL, item.socket->descriptor, &ev);
#elif defined(_MAC)
                struct kevent kev = {0};
                kev.ident = item.socket->descriptor;
                kev.flags = EV_DELETE;
                kev.filter = EVFILT_READ;
                kevent(wd, &kev, 1, 0, 0, 0);
#endif
                uint32 pos = workList.indexOf(&item);
                item.socket->setOption(BaseSocket::Cork, 0);
                if(item.file >= 0) { close(item.file); item.file = -1; }
                if (pos != workList.getSize()) workList.Remove(pos);
#endif
                count--;
            }
            void tellSuccess(Item & item, uint32 i, uint32 & count)
            {
                // No lock her fore the callbacks : The remove of the list managed the protections
                // Please see removeItem
                callbackList.Append(Callback(item.socket, item.callback, item.sent));
#ifndef _POSIX
                workList.Remove(i);
#else
#if defined(_LINUX)
                struct epoll_event ev = {0};
                epoll_ctl(wd, EPOLL_CTL_DEL, item.socket->descriptor, &ev);
#elif defined(_MAC)
                struct kevent kev = {0};
                kev.ident = item.socket->descriptor;
                kev.flags = EV_DELETE;
                kev.filter = EVFILT_READ;
                kevent(wd, &kev, 1, 0, 0, 0);
#endif
                uint32 pos = workList.indexOf(&item);
                item.socket->setOption(BaseSocket::Cork, 0);
                if(item.file >= 0) { close(item.file); item.file = -1; }
                if (pos != workList.getSize()) workList.Remove(pos);
#endif
                count--;
            }

            virtual uint32 runThread()
            {
                while (isRunning())
                {
                    // Stop the process on remove asked
                    if (removeRequired.Wait(Threading::InstantCheck))
                    {
                        mainLoopWaiting.Set();
                        removeDone.Wait(Threading::Infinite);
                    }

                    if ((!workList.getSize() && pendingListEvent.Wait(20)) )
                    {
                        movePendingListIntoWorkingList();
                        // Reset event
                        pendingListEvent.Reset();
                    }
                    else if(workList.getSize() && pendingListEvent.Wait(Threading::InstantCheck))
                    {
                        movePendingListIntoWorkingList();
                        // Reset event
                        pendingListEvent.Reset();
                    }
#ifdef _WIN32
                    // Need to build a list of objects to wait on
                    HANDLE list[MAXIMUM_WAIT_OBJECTS];
                    uint32 processed = 0;
                    while (processed < workList.getSize())
                    {
                        uint32 count = min((uint32)MAXIMUM_WAIT_OBJECTS, (uint32)workList.getSize() - processed);
                        for (uint32 i = 0; i < count; i++)
                            list[i] = workList[processed + i].over.hEvent;

                        DWORD ret = WaitForMultipleObjects(count, list, FALSE, 40);
                        if (ret >= WAIT_OBJECT_0 && ret < (WAIT_OBJECT_0 + count))
                        {
                            // Ok, at least one of them finished, so let's trigger the callback now
                            uint32 index = ret - WAIT_OBJECT_0;
                            Item & item = workList[index + processed];
                            DWORD dwRead;
                            if (GetOverlappedResult((HANDLE)item.socket->descriptor, &item.over, &dwRead, FALSE) == FALSE)
                                tellError(item, index + processed, count);
                            else
                            {
                                if (dwRead >= item.remaining)
                                {
                                    item.sent += dwRead;
                                    item.remaining -= dwRead;
                                    tellSuccess(item, index + processed, count);
                                }
                                else
                                {
                                    // Check if we overcame the first limit
                                    if (item.transmit[0].dwElFlags == TP_ELEMENT_MEMORY && dwRead >= item.transmit[0].cLength)
                                    {
                                        // We overcame the first buffer, let's remove it from the list
                                        uint64 newOffset = item.transmit[0].cLength, newSize = item.remaining - dwRead - item.transmit[2].cLength;
                                        item.sent += dwRead;
                                        item.remaining -= dwRead;
                                        item.used --;
                                        memcpy(&item.transmit[0], &item.transmit[1], item.used * sizeof(item.transmit[1]));
                                        item.transmit[0].nFileOffset.QuadPart += (dwRead - newOffset);
                                        item.transmit[0].cLength = (ULONG)(newSize > 0xFFFFFFFF ? 0xFFFFFFFF : newSize);
                                        if (MyTransmitPackets(item.socket->descriptor, item.transmit, item.used, 0, &item.over, TF_USE_KERNEL_APC) == FALSE)
                                            tellError(item, index + processed, count);
                                    }
                                    else if (item.transmit[0].dwElFlags == TP_ELEMENT_FILE && dwRead >= item.transmit[0].cLength && item.transmit[0].cLength)
                                    {
                                        uint64 newOffset = item.transmit[0].cLength, newSize = item.remaining - dwRead - item.transmit[2].cLength;
                                        item.sent += dwRead;
                                        item.remaining -= dwRead;
                                        item.transmit[0].nFileOffset.QuadPart += dwRead;
                                        item.transmit[0].cLength = (ULONG)(newSize > 0xFFFFFFFF ? 0xFFFFFFFF : newSize);
                                        if (MyTransmitPackets(item.socket->descriptor, item.transmit, item.used, 0, &item.over, TF_USE_KERNEL_APC) == FALSE)
                                            tellError(item, index + processed, count);
                                    }
                                    else
                                        tellError(item, index + processed, count);
                                }
                            }
                        }
                        else if (ret >= WAIT_ABANDONED_0 && ret < (WAIT_ABANDONED_0 + count))
                        {
                            // Ok, at least one of them errored, so let's trigger the callback now too
                            uint32 index = ret - WAIT_ABANDONED_0;
                            Item & item = workList[index + processed];
                            tellError(item, index + processed, count);
                        }

                        processed += count;
                    }
#elif defined(_POSIX)
                    // We need to monitor all the pending socket at once
#if defined(_LINUX)
                    triggerCount = epoll_wait(wd, (struct epoll_event*)events, workList.getSize(), 20);
#elif defined(_MAC)
                    struct timespec timeout = {0}; timeout.tv_sec = 0; timeout.tv_nsec = 20 * 1000 * 1000;
                    triggerCount = kevent(wd, 0, 0, (struct kevent*)events, workList.getSize(), &timeout);
#endif
                    if (triggerCount > 0)
                    {
                        uint32 count = (uint32)triggerCount;
                        for (uint32 index = 0; index < (uint32)triggerCount; index++) // Expressively not count
                        {
                            // Ok, then iterate the socket pool, and send it's data
#if defined(_LINUX)
                            Item * item = (Item*)events[index].data.ptr;
                            if (item)
                            {
                                bool stop = false;
                                while (item->sent < item->prefixLength)
                                {
                                    int ret = item->socket->send((const char*)&item->prefixBuffer[item->sent], item->prefixLength - item->sent, 0);
                                    if (ret > 0) item->sent += ret;
                                    if (ret == -1)
                                    {
                                        // Tell error and delete item
                                        stop = true;
                                        if (errno == EAGAIN) break; // It would block to send the buffer, so let's move on to the next socket
                                        tellError(*item, -1, count);
                                        break;
                                    }
                                }
                                while (!stop && item->sent >= item->prefixLength && item->size > 0)
                                {
                                    off_t offset = item->offset;
                                    int ret = ::sendfile(item->socket->descriptor, item->file, &offset, item->size > 0x80000000 ? 0x7FFFFFFF : item->size);
                                    if (ret > 0) { item->sent += ret; item->size -= ret; item->offset = offset; }
                                    if (ret == -1)
                                    {
                                        if (errno == EAGAIN) { ret = offset - item->offset; item->sent += ret; item->size -= ret; item->offset = offset; break; }
                                        // Tell error and delete item
                                        stop = true;
                                        tellError(*item, -1, count);
                                        break;
                                    }
                                }
                                while (!stop && item->size == 0 && item->sent >= (item->prefixLength + item->offset) && item->sent < (item->prefixLength + item->offset + item->suffixLength))
                                {
                                    int size =  item->prefixLength + item->offset + item->suffixLength - item->sent;
                                    int ret = item->socket->send((const char*)&item->suffixBuffer[item->sent - item->prefixLength - item->offset], size, 0);
                                    if (ret > 0) item->sent += ret;
                                    if (ret == -1)
                                    {
                                        if (errno == EAGAIN) break;
                                        // Tell error and delete item
                                        stop = true;
                                        tellError(*item, -1, count);
                                        break;
                                    }
                                }
                                if (!stop && item->sent == (item->prefixLength + item->offset + item->suffixLength))
                                {
                                    // Successfully sent all data
                                    tellSuccess(*item, -1, count);
                                }
                            }
#elif defined(_MAC)
                            // On MAC, sendfile handles prefix and suffix data automatically too.
                            Item * item = (Item*)events[index].udata;
                            bool stop = false;
                            if (item)
                            {
                                struct sf_hdtr headerTrailer = {0};
                                struct iovec vec[2] = {};

                                off_t totalLength = (off_t)item->prefixLength + (off_t)item->size + (off_t)item->suffixLength;
                                off_t headerFile = (off_t)item->prefixLength + (off_t)item->size;
                                bool hasHeader = item->sent < item->prefixLength;
                                vec[0].iov_base = (void*)(hasHeader ? &item->prefixBuffer[item->sent] : 0);
                                vec[0].iov_len = max((int)(item->prefixLength - item->sent), 0);

                                bool hasTrailer = item->sent > headerFile && item->suffixLength > 0;
                                vec[1].iov_base = (void*)(hasTrailer ? &item->suffixBuffer[item->sent - headerFile] : 0);
                                vec[1].iov_len = max((int)(item->suffixLength - (item->sent - headerFile)), 0);

                                headerTrailer.headers = hasHeader ? &vec[0] : NULL;
                                headerTrailer.hdr_cnt = hasHeader ? 1 : 0;
                                headerTrailer.trailers = hasTrailer ? &vec[1] : NULL;
                                headerTrailer.trl_cnt = hasTrailer ? 1 : 0;

                                off_t sent = 0;
                                while (sent < totalLength)
                                {
                                    off_t offset = item->offset + item->prefixLength;
                                    off_t iterSent = item->size;
                                    int ret = ::sendfile(item->file, item->socket->descriptor, offset, &iterSent, &headerTrailer, 0);

                                    // Handle prefix
                                    int prefixSent = iterSent < item->prefixLength ? iterSent : item->prefixLength;
                                    item->sent += prefixSent;
                                    iterSent -= prefixSent;

                                    // Handle file too
                                    item->offset += iterSent; item->size -= iterSent;
                                    item->sent += iterSent;

                                    if (ret != 0)
                                    {
                                        if (errno == EAGAIN) break;
                                        // Tell error delete item
                                        stop = true;
                                        tellError(*item, -1, count);
                                        break;
                                    }
                                }
                                if (!stop && sent == totalLength)
                                    tellSuccess(*item, -1, count);
                            }
#endif
                        }
                    }
#endif

                    // Call back with no lock taken
                    triggerCallbacks();
                }
                return 0;
            }

            WaitingThread() : lockPendingList("lockPendingList"), pendingListEvent("AddInPendingListEvent"),
                removeRequired("removeRequired", Threading::Event::AutoReset), mainLoopWaiting("mainLoopWaiting", Threading::Event::AutoReset), removeDone("removeDone", Threading::Event::AutoReset), lockRemove("lockRemove")

#ifdef _POSIX
                , wd(-1), triggerCount(0), events(0)
#endif
            { }

            ~WaitingThread()
            {
                destroyThread();
#ifdef _POSIX
                if(wd >= 0) { close(wd); wd = -1; }
                Platform::safeRealloc(events, 0);
#endif
            }

        };

        WaitingThread & getWaitingThread()
        {
            static WaitingThread thread;
            return thread;
        }

        // Send data and file asynchronously on the socket.
        bool  BerkeleySocket::sendDataAndFile(const char * prefixBuffer, const int prefixLength, const String & filePath, const uint64 offset, const uint64 size,
                                                const char * suffixBuffer, const int suffixLength, BaseSocket::SDAFCallback & callback)
        {
#ifdef NEXIO
            return false;
#else
            return getWaitingThread().appendItem(prefixBuffer, prefixLength, filePath, offset, size, suffixBuffer, suffixLength, callback, this);
#endif
        }

        // Cancel an asynchronous sending
        bool BerkeleySocket::cancelAsyncSend()
        {
#ifdef NEXIO
            return false;
#else
            return getWaitingThread().removeItem(this);
#endif
        }

        // Bind on a multicast interface.
        bool BerkeleySocket::bindMulticastInterface(const Address & interfaceAddress)
        {
            CaptureCall;
            // If not open yet, open now
            if (descriptor == SocketError && !open(false)) Return(false);

            int mcastHops = 2; // Set a TTL of 2
            if (interfaceAddress.getTypeID() == 2)
            {   // IPV6
                if (interfaceAddress.isValid())
                {
                    uint32 ifindex = ::if_nametoindex(interfaceAddress.asText());
                    if (setsockopt(descriptor, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char *)&ifindex, sizeof(ifindex)) != 0) Return(false);
                }

                if (setsockopt(descriptor, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char *)&mcastHops, sizeof(mcastHops)) != 0) Return(false);
            }
            else
            {
                if (interfaceAddress.isValid())
                {
                    if (setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_IF, (const char *)interfaceAddress.getLowLevelObject(), interfaceAddress.getLowLevelObjectSize()) != 0) Return(false);
                }
#ifdef _WIN32
                unsigned long _ttl = mcastHops;
                if(setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&_ttl, sizeof(_ttl)) != 0) Return(false);
#else
                uint8 _ttl = mcastHops;
                if(setsockopt(descriptor, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&_ttl, sizeof(_ttl)) != 0) Return(false);
#endif
            }

            if (interfaceAddress.getPort())
            {
                Network::Address::IPV6 any6; Network::Address::IPV4 any4;
                Address * any = interfaceAddress.getTypeID() == 2 ? (Address*)&any6 : (Address*)&any4;
                any->setPort(interfaceAddress.getPort());
                Return(bind(*any, false) == Success);
            }

            Return(true);
        }


        // Set the socket option
        bool BerkeleySocket::setOption(const Option option, const int value)
        {
            CaptureCall;
            // If not open yet, open now
            if (descriptor == SocketError && !open(false)) Return(false);

            switch(option)
            {
            case Blocking:
                {
                    int _value = !value;
#ifdef _WIN32
                    // Set non blocking socket now
                    if (ioctlsocket(descriptor, FIONBIO, (u_long FAR*) &_value) != 0) Return(false);
#endif
#ifdef NEXIO
                    // Set non blocking socket now
                    if (ioctlsocket(descriptor, FIONBIO,  &_value) != 0) Return(false);
#endif
#ifdef _POSIX
                    int socketFlags = fcntl(descriptor, F_GETFL, 0);
                    if (socketFlags == -1) Return(false);
                    if (fcntl(descriptor, F_SETFL, (socketFlags & ~O_NONBLOCK) | (_value * O_NONBLOCK)) != 0) Return(false);
#endif
                    ReturnI(true, Blocking, _value);
                }
            case ReceiveBufferSize:
                ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_RCVBUF, (const char*)&value, sizeof(value)) == 0, ReceiveBufferSize, value);
            case SendBufferSize:
                ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_SNDBUF, (const char*)&value, sizeof(value)) == 0, SendBufferSize, value);
            case ReuseAddress:
                ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&value, sizeof(value)) == 0, ReuseAddress, value);
            case LingerOnClose:
                {
                    struct linger ling = {0};
                    ling.l_onoff = value;
                    ling.l_linger = 3;
                    ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_LINGER, (const char*)&ling, sizeof(ling)) == 0, LingerOnClose, value);
                }
            case Broadcast:
                ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_BROADCAST, (const char*)&value, sizeof(value)) == 0, Broadcast, value);
            case NoDelay:
                ReturnI(setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, (const char*)&value, sizeof(value)) == 0, NoDelay, value);
            case TCPMaxSeg:
                ReturnI(setsockopt(descriptor, IPPROTO_TCP, TCP_MAXSEG, (const char*)&value, sizeof(value)) == 0, TCPMaxSeg, value);
            case Cork:
#ifdef _LINUX
                ReturnI(setsockopt(descriptor, IPPROTO_TCP, TCP_CORK, (const char*)&value, sizeof(value)) == 0, Cork, value);
#elif defined(_MAC)
                ReturnI(setsockopt(descriptor, IPPROTO_TCP, TCP_NOPUSH, (const char*)&value, sizeof(value)) == 0, Cork, value);
#else
                Return(false);
#endif

            case NoSigPipe:
#ifdef _LINUX
                sendOptions = value ? MSG_NOSIGNAL : 0;
                Return(true);
#elif defined(_MAC)
                ReturnI(setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&value, sizeof(value)) == 0, NoSigPipe, value);
#else
                Return(false);
#endif
            // Update the descriptor directly
            case Descriptor:
                // Close the current descriptor if it's still valid and being asked to do so
                // The only case we don't close it, is if the value passed in is SocketError (in that case, we forget the descriptor)
                if (descriptor != SocketError && value != SocketError) ::close(descriptor);
                descriptor = (sock_t)value;
                ReturnI(true, Descriptor, value);

            // Not handled in Berkeley
            case SendTimeout:
            case ReceiveTimeout:
            case RendezVous:
                break;
            }

            Return(false);
        }

        // Set the socket option
        bool BerkeleySocket::getOption(const Option option, int & value)
        {
            CaptureCall;
            // If not open yet, open now
            if (descriptor == SocketError && !open(false)) Return(false);
            socklen_t len = sizeof(value);
            switch(option)
            {
            case ReceiveBufferSize:
                ReturnI(getsockopt(descriptor, SOL_SOCKET, SO_RCVBUF,     (char*)&value, &len) == 0, ReceiveBufferSize, value);
            case SendBufferSize:
                ReturnI(getsockopt(descriptor, SOL_SOCKET, SO_SNDBUF,     (char*)&value, &len) == 0, SendBufferSize, value);
            case ReuseAddress:
                ReturnI(getsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,  (char*)&value, &len) == 0, ReuseAddress, value);
            case Broadcast:
                ReturnI(getsockopt(descriptor, SOL_SOCKET, SO_BROADCAST,  (char*)&value, &len) == 0, Broadcast, value);
            case NoDelay:
                ReturnI(getsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY,  (char*)&value, &len) == 0, NoDelay, value);
            case TCPMaxSeg:
                ReturnI(getsockopt(descriptor, IPPROTO_TCP, TCP_MAXSEG,   (char*)&value, &len) == 0, TCPMaxSeg, value);
            case Cork:
#ifdef _LINUX
                ReturnI(getsockopt(descriptor, IPPROTO_TCP, TCP_CORK,     &value, &len) == 0, Cork, value);
#elif defined(_MAC)
                ReturnI(getsockopt(descriptor, IPPROTO_TCP, TCP_NOPUSH,   &value, &len) == 0, Cork, value);
#endif
            // Might not work on some platform
            case Descriptor: value = (int)descriptor; Return(true);
            // Not handled in Berkeley
            default: Return(false);
            }
            Return(false);
        }


        // Select on this socket.
        bool BerkeleySocket::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
        {
            CaptureCall;
            if (descriptor == SocketError) Return(false);

            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;
            fd_set set;
            FD_ZERO(&set);
            FD_SET(descriptor, &set);
            int ret = 0;
            // Then select
            while ((ret = ::select(FD_SETSIZE, reading ? &set : NULL, writing ? &set : NULL, NULL, timeout < 0 ? NULL : &tv)) == -1)
            {
#ifdef _WIN32
                if (WSAGetLastError() != WSAEINTR) { timeout.failed(-WSAGetLastError()); Return(false); }
#else
                if (errno != EINTR) { timeout.failed(-errno); Return(false); }
#endif
                if (timeout.timedOut()) Return(false);
            }
            // In case of timeout, simple modify errno so we can detect this case
            timeout.filterError(ret);
            if (ret == 0) setError(InProgress);
            Return(ret >= 1);
        }
        // Append this socket to a monitoring pool (so it's possible to select over multiple socket later on)
        MonitoringPool * BerkeleySocket::appendToMonitoringPool(MonitoringPool * pool)
        {
            if (!pool)
            {
                pool = new BerkeleyPool();
                if (!pool) return 0;
                if (!pool->appendSocket(this))
                {
                    delete pool;
                    return 0;
                }
                return pool;
            }

            if (!pool->appendSocket(this)) return 0;
            return pool;
        }
        // Get the peer name for a connected socket
        Address * BerkeleySocket::getPeerName() const
        {
            char buffer[sizeof(struct sockaddr_in6)];
            socklen_t socklen = sizeof(buffer);
            if (getpeername(descriptor, (struct sockaddr*)buffer, &socklen) == 0)
            {
                if (socklen == sizeof(struct sockaddr_in))
                {   // IPv4
                    struct sockaddr_in * addr = (struct sockaddr_in *)buffer;
                    return new Network::Address::IPV4(ntohl(addr->sin_addr.s_addr), ntohs(addr->sin_port));
                } else
                {   // IPv6
                    struct sockaddr_in6 * addr = (struct sockaddr_in6 *)buffer;
                    return new Network::Address::IPV6((const uint8 *)addr->sin6_addr.s6_addr, (uint16)ntohs(addr->sin6_port));
                }
            }
            return 0;
        }
        // Get the bound address for a connected socket
        Address * BerkeleySocket::getBoundAddress() const
        {
            char buffer[sizeof(struct sockaddr_in6)];
            socklen_t socklen = sizeof(buffer);
            if (getsockname(descriptor, (struct sockaddr*)buffer, &socklen) == 0)
            {
                if (socklen == sizeof(struct sockaddr_in))
                {   // IPv4
                    struct sockaddr_in * addr = (struct sockaddr_in *)buffer;
                    return new Network::Address::IPV4((uint32)ntohl(addr->sin_addr.s_addr), (uint16)ntohs(addr->sin_port));
                } else
                {   // IPv6
                    struct sockaddr_in6 * addr = (struct sockaddr_in6 *)buffer;
                    return new Network::Address::IPV6((const uint8 *)addr->sin6_addr.s6_addr, (uint16)ntohs(addr->sin6_port));
                }
            }
            return 0;
        }

#if _LINUX == 1
// Open a socket of the specified type
bool EthernetSocket::open(const Type type)
{
    if (!close()) return false;
    if (!local.isValid()) return false;
    // Check if the local address match a local interface
    String adapterName = local.getAdapterName();
    if (!adapterName) return false;

    if (type == Unknown)
    {   // That's for sending
        if ((descriptor = socket(PF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) return false;
        // Need to find the local interface index
        struct ifreq ifopts = {}; // To set promiscuous mode
        // Set interface to promiscuous mode
        strncpy(ifopts.ifr_name, adapterName, IFNAMSIZ-1);
        // Fetch flags first before adding ours
    	if (ioctl(descriptor, SIOCGIFINDEX, &ifopts) < 0) return false;
        localIFIndex = ifopts.ifr_ifindex;

        return true; // For now
    }
    else
    {   // Receiving
        if ((descriptor = socket(PF_PACKET, SOCK_RAW, htons((uint16)local.getProtocol()))) == -1) return false;
        // Then set the available options
        struct ifreq ifopts = {}; // To set promiscuous mode
        // Set interface to promiscuous mode
        strncpy(ifopts.ifr_name, adapterName, IFNAMSIZ-1);
        // Fetch flags first before adding ours
    	if (ioctl(descriptor, SIOCGIFFLAGS, &ifopts) < 0) return false;
        ifopts.ifr_flags |= IFF_PROMISC;
        if (ioctl(descriptor, SIOCSIFFLAGS, &ifopts) < 0) return false;


        // Allow the socket to be reused - incase connection is closed prematurely
        if (!setOption(ReuseAddress, true)) return false;
        // Actually bind to device
        if (setsockopt(descriptor, SOL_SOCKET, SO_BINDTODEVICE, (const char*)adapterName, IFNAMSIZ-1) == -1)	 return false;

        // Now, we're done
        return true;
    }
}
// Close the socket
bool EthernetSocket::close()
{
    if (descriptor != SocketError) ::close(descriptor);
    descriptor = SocketError;
    return true;
}

// Bind a socket on the given address
BaseSocket::Error EthernetSocket::bind(const Address & address, const bool allowBroadcast)
{
    if (address.getTypeID() != 4) return setError(BadArgument);
    local = address;
    return setError(Success);
}
// Connect a socket to the given address
BaseSocket::Error EthernetSocket::connect(const Address & address)
{
    if (address.getTypeID() != 4) return setError(BadArgument);
    dest = address;
    return setError(Success);
}
// Receive data on the socket
int EthernetSocket::receive(char * buffer, const int bufferSize, const int flags) const
{
    if (!local.isValid() || !buffer) return -1;
    if (descriptor == SocketError || !wasReceiving)
    {
        wasReceiving = true;
        if (!const_cast<EthernetSocket*>(this)->open(Raw)) return -1; // Could not open the raw socket
    }
    // Then receive on the socket. By default, the ethernet header is not included in the answer.
    // If you need this header, use a flags set to MSG_OOB to get them
    if (flags & MSG_OOB)
        return setError(::recv(descriptor, buffer, bufferSize, 0));

    // Need to skip ethernet header from receiving
    char buf[9030]; // Jumbo frame
    const int ethernetHeaderSize = 14, jumboEthernetHeaderSize = 18;
    int size = ::recv(descriptor, buf, ArrSz(buf), 0);
    if (size > 0)
    {
        int packetSize = size - ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize + 4 : ethernetHeaderSize + 4); // For checksum
        if (packetSize < 0) return 0;
        memcpy(buffer, buf + ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize : ethernetHeaderSize), packetSize);
        return packetSize;
    }

    return 0;
}

// Actually open the socket
bool EthernetSocket::openFor(const bool receiving)
{
    if (!local.isValid()) return false;
    if (receiving)
    {
        if (descriptor == SocketError || !wasReceiving)
        {
            wasReceiving = true;
            return open(Raw);
        }
        return false;
    }
    if (!dest.isValid() || dest.getTypeID() != 4) return false;
    if (descriptor == SocketError || wasReceiving)
    {
        wasReceiving = false;
        return open(Unknown);
    }
    return true;
}

// Receive data on the socket using a loop
int EthernetSocket::receiveCB(ReceiveCallback & cb, const Time::TimeOut & timeout)
{
    if (!local.isValid()) return -1;
    if (descriptor == SocketError || !wasReceiving)
    {
        wasReceiving = true;
        if (!const_cast<EthernetSocket*>(this)->open(Raw)) return -1; // Could not open the raw socket
    }

    if (!setOption(Blocking, 0)) return -1;

    char buf[9030]; // Jumbo frame
    const int ethernetHeaderSize = 14, jumboEthernetHeaderSize = 18;
    int innerCount = 0, outerCount = 0;
    while (!outerCount || select(true, false, timeout)) // Skip first lookup
    {
        // This inner loop takes less time since we are only doing a single syscall between packets
        outerCount++;
        while (true)
        {
            int size = ::recv(descriptor, buf, ArrSz(buf), 0);
            if (size == -1 && errno == EAGAIN) break;
            innerCount++;
            int packetSize = size - ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize + 4 : ethernetHeaderSize + 4); // For checksum
            if (packetSize > 0 && !cb.receive(buf, size, buf + ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize : ethernetHeaderSize)))
            {
                Logger::log(Logger::Network, "Receive stat: %d %d", innerCount, outerCount);
                return setOption(Blocking, 1) ? 1 : -1;
            }
        }
    }
    return setOption(Blocking, 1) ? 0 : -1;
}

// Receive data on the datagram socket.
int EthernetSocket::receiveFrom(char * buffer, const int bufferSize, Address *& from) const
{
    int ret = receive(buffer, bufferSize, MSG_OOB);
    const int ethernetHeaderSize = 14, jumboEthernetHeaderSize = 18;
    const int packetSize = ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize + 4 : ethernetHeaderSize + 4);
    if (ret <= packetSize) return ret;

    delete from;
    from = new EthernetAddress((const uint8*)&buffer[6], 6, BigEndian(*(uint16*)&buffer[12]));
    memmove(buffer, buffer + ((EthernetAddress::EthProtocols)local.getProtocol() == EthernetAddress::VLAN ? jumboEthernetHeaderSize : ethernetHeaderSize), packetSize);

    return packetSize;
}
// Send data on the datagram socket.
int EthernetSocket::sendTo(const char * buffer, const int bufferSize, const Address & to) const
{
    if (!local.isValid()) return -1;
    if (!to.isValid() || to.getTypeID() != 4 || !buffer || bufferSize > 9008) return -1;
    const EthernetAddress & _dst = static_cast<const EthernetAddress &>(to);

    if (descriptor == SocketError || wasReceiving)
    {
        wasReceiving = false;
        if (!const_cast<EthernetSocket*>(this)->open(Unknown)) return -1; // Could not open the raw socket
    }

    // Need to build the ethernet header
    char buf[9030]; // Jumbo frame
    const int ethernetHeaderSize = 14, jumboEthernetHeaderSize = 18;
    // Header
    memcpy(&buf[0], _dst.getLowLevelObject(), 6);
    memcpy(&buf[6], local.getLowLevelObject(), 6);
    uint16 type = BigEndian((uint16)_dst.getProtocol());
    memcpy(&buf[12], &type, 2);
    // Payload
    memcpy(&buf[16], buffer, bufferSize);
    // Address to send to
    struct sockaddr_ll socketAddress = {};
    socketAddress.sll_ifindex = localIFIndex;
    socketAddress.sll_halen = ETH_ALEN; // No VLAN, you have to emulate this in your payload if you need to
    memcpy(socketAddress.sll_addr, &buf[0], 6);

    return setError(::sendto(descriptor, buf, bufferSize + 16, 0, (struct sockaddr*)&socketAddress, sizeof(struct sockaddr_ll)));
}
// Send data on the socket
int EthernetSocket::send(const char * buffer, const int bufferSize, const int flags) const
{
    if (!dest.isValid() || local.getProtocol() != dest.getProtocol()) return -1;
    return sendTo(buffer, bufferSize, dest);
}
// Send data from multiple buffer at once.
int EthernetSocket::sendBuffers(const char ** buffers, const int * buffersSize, const int buffersCount, const int flags) const
{
    return -1; //For now
}
// Send data from multiple buffer at once to the specified address (this only works for datagram sockets).
int EthernetSocket::sendBuffersTo(const char ** buffers, const int * buffersSize, const int buffersCount, const Address & to) const
{
    return -1; //For now
}
// Set the socket option
bool EthernetSocket::setOption(const Option option, const int value)
{
    if (descriptor == SocketError) return false;
    switch (option)
    {
    case ReuseAddress: return setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, (const char*)&value, sizeof(value)) == 0;
    case ReceiveBufferSize: return setsockopt(descriptor, SOL_SOCKET, SO_RCVBUFFORCE, (const char*)&value, sizeof(value)) == 0;
    case Blocking:
    {
        int _value = !value;
        int socketFlags = fcntl(descriptor, F_GETFL, 0);
        if (socketFlags == -1) return false;
        return fcntl(descriptor, F_SETFL, (socketFlags & ~O_NONBLOCK) | (_value * O_NONBLOCK)) == 0;
    }

    default: return false;
    }
}
// Get the socket option
bool EthernetSocket::getOption(const Option option, int & value)
{
    return false;
}
// Select on this socket.
bool EthernetSocket::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
{
    if (descriptor == SocketError)
    {
        if (reading && !wasReceiving)
        {
            wasReceiving = true;
            if (!const_cast<EthernetSocket*>(this)->open(Raw)) return false; // Could not open the raw socket
        }
        else if (writing && wasReceiving)
        {
            wasReceiving = false;
            if (!const_cast<EthernetSocket*>(this)->open(Unknown)) return false; // Could not open the raw socket
        }
    }

    struct timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(descriptor, &set);
    int ret = 0;
    // Then select
    while ((ret = ::select(FD_SETSIZE, reading ? &set : NULL, writing ? &set : NULL, NULL, timeout < 0 ? NULL : &tv)) == -1)
    {
        if (errno != EINTR) { timeout.failed(-errno); return false; }
        if (timeout.timedOut()) return false;
    }
    // In case of timeout, simple modify errno so we can detect this case
    timeout.filterError(ret);
    if (ret == 0) setError(InProgress);
    return ret >= 1;
}

#endif

#ifdef UsingUDT
        // Append a socket to this pool
        bool UDTPool::appendSocket(BaseSocket * _socket)
        {
            UDTSocket * socket = dynamic_cast<UDTSocket*>(_socket);
            if (size >= MaxQueueLen) return false;
            if (!socket) return false;
            UDTSocket ** newPool = (UDTSocket **)Platform::safeRealloc(pool, (size+1) * sizeof(*pool));
            if (newPool == NULL) return false;
            pool = newPool;
            pool[size++] = socket;
            return true;
        }
        // Remove a socket from the pool
        bool UDTPool::forgetSocket(BaseSocket * _socket)
        {
            UDTSocket * socket = dynamic_cast<UDTSocket*>(_socket);
            if (!socket) return false;
            // Need to find out the socket in the pool
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket)
                {   // Found
                    // Swap the value with the last on in the list
                    pool[i] = pool[size - 1];
                    size--;
                    pool[size] = 0;
                    // We don't realloc smaller anyway
                    return true;
                }
            }
            return false;
        }
        // Remove a socket from the pool
        bool UDTPool::removeSocket(BaseSocket * _socket)
        {
            if (!forgetSocket(_socket)) return false;
            if (own) delete (UDTSocket*)_socket;
            return true;
        }
        // Get the pool size
        uint32 UDTPool::getSize() const { return size; }

        // Select the pool for at least an element that is ready
        bool UDTPool::select(const bool reading, const bool writing, const Time::TimeOut & timeout) const
        {
            if (timeout <= 0) return false; // Already timed out previously

            // Need to clear the sets, and start
            UD_ZERO(&rset); UD_ZERO(&wset);
            if (reading)
                for (uint32 i = 0; i < size; i++) { if (pool[i]->descriptor != UDTSocket::SocketError) UD_SET(pool[i]->descriptor, &rset); }
            if (writing)
                for (uint32 i = 0; i < size; i++) { if (pool[i]->descriptor != UDTSocket::SocketError) UD_SET(pool[i]->descriptor, &wset); }

            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;

            int ret = 0;
            // Then select
            while ((ret = UDT::select(FD_SETSIZE, reading ? &rset : NULL, writing ? &wset : NULL, NULL, timeout < 0 ? NULL : &tv)) == UDT::ERROR)
            {
                if (UDT::getlasterror() != EINTR) return false;
                if (timeout.timedOut()) return false;
            }
            timeout.filterError(ret);
            return ret >= 1;
        }

        // Check if at least a socket in the pool is ready for reading
        bool UDTPool::isReadPossible(const Time::TimeOut & timeout)  const { return select(true, false, timeout); }
        // Check if at least a socket in the pool is ready for writing
        bool UDTPool::isWritePossible(const Time::TimeOut & timeout) const { return select(false, true, timeout); }
        // Check if a socket is connected.
        bool UDTPool::isConnected(const Time::TimeOut & timeout)
        {
            for (uint32 i = 0; i < size; i++) pool[i]->setOption(BaseSocket::Blocking, 0);
            bool ret = isReadPossible(timeout);
            for (uint32 j = 0; j < size; j++) pool[j]->setOption(BaseSocket::Blocking, 1);
            return ret;
        }

        // Check which socket was ready in the given pool
        int UDTPool::getNextReadySocket(const int index) const
        {
            for (uint32 i = index+1; i < size; i++)
            {
                if (UD_ISSET(pool[i]->descriptor, &rset) || UD_ISSET(pool[i]->descriptor, &wset))
                    return (int)i;
            }
            return -1;
        }
        // Get the socket at the given position
        BaseSocket * UDTPool::operator[] (const int index) { return index >= 0 && index < (int)size ?  pool[index] : 0; }
        // Get the socket at the given position
        BaseSocket * UDTPool::getReadyAt(const int index)  { return index >= 0 && index < (int)size ?  pool[index] : 0; }
        // Check if we already have the given socket in the pool
        bool UDTPool::haveSocket(BaseSocket * socket) const
        {
            for (uint32 i = 0; i < size; i++)
            {
                if (pool[i] == socket) return true;
            }
            return false;
        }

        // Select our for reading for reading, and the other pool for writing.
        int UDTPool::selectMultiple(MonitoringPool * _other, const Time::TimeOut & timeout) const
        {
            if (timeout <= 0) return false; // Already timed out previously

            UDTPool * other = dynamic_cast<UDTPool*>(_other);
            if (!other) return 0;

            // Need to clear the sets, and start
            UD_ZERO(&rset); UD_ZERO(&other->wset);
            for (uint32 i = 0; i < size; i++) { if (pool[i]->descriptor != UDTSocket::SocketError) UD_SET(pool[i]->descriptor, &rset); }
            for (uint32 j = 0; j < other->size; j++) { if (pool[i]->descriptor != UDTSocket::SocketError) UD_SET(other->pool[j]->descriptor, &other->wset); }

            struct timeval tv;
            tv.tv_sec = timeout / 1000;
            tv.tv_usec = (timeout % 1000) * 1000;

            int ret = 0;
            // Then select
            while ((ret = UDT::select(FD_SETSIZE, &rset, &other->wset, NULL, timeout < 0 ? NULL : &tv)) == UDT::ERROR)
            {
                if (UDT::getlasterror() != EINTR) return 0;
            }
            return ret >= 1;
        }
        UDTPool::~UDTPool()
        {
            if (own)
            {
                // Destruct all the owned sockets
                for (uint32 i = 0; i < size; i++) { delete pool[i]; pool[i] = 0; }
            }
            size = 0;
            Platform::safeRealloc(pool, 0);
            pool = 0;
        }
#endif



        // Check if a connection from a remote end is pending.
        bool Local::hasConnectionPending(const Time::TimeOut & timeout)
        {
            switch(socket.getState())
            {
            case BaseSocket::Unset:
                if (!socket.open(socket.getType() == BaseSocket::Datagram ? BaseSocket::Datagram : BaseSocket::Stream)) return false;
            case BaseSocket::Opened:
                if (address.getPort())
                    if (!socket.bind(address)) return false;
            case BaseSocket::Bound:
                if (!socket.listen(-1)) return false;
            default:
                break;
            }
            return socket.select(true, false, timeout);
        }

        // Connect to the given remote address.
        bool Local::connectTo(const Address & remote)
        {
            switch(socket.getState())
            {
            case BaseSocket::Unset:
                if (!socket.open(socket.getType() == BaseSocket::Datagram ? BaseSocket::Datagram : BaseSocket::Stream)) return false;
            case BaseSocket::Opened:
                if (address.getPort())
                    if (!socket.bind(address)) return false;
            default:
                break;
            }
            if (!socket.setOption(BaseSocket::Blocking, 0)) return false;
            BaseSocket::Error error = socket.connect(remote);
            return error == BaseSocket::Success || error == BaseSocket::InProgress;
        }
        // Check if a connection to a remote host succeeded in the given amount of time.
        Connection * Local::isConnectedToRemote(const Time::TimeOut & timeout)
        {
            if (socket.getState() == BaseSocket::Connecting && socket.select(false, true, timeout))
            {
                // Connection is pending, so let's create a new connection
                socket.connectionDone();
                socket.setState(BaseSocket::Connected);
            }
            if (socket.getState() == BaseSocket::Connected)
            {
                switch(socket.getType())
                {
                case BaseSocket::Datagram:
                    return new Network::Connection::Datagram(*this, socket.getPeerName());
                case BaseSocket::Stream:
                    return new Network::Connection::Stream(*this, socket.getPeerName());
                default:
                    return 0;
                }
            }
            return 0;
        }

        // Accept a connection on a listening socket.
        Connection * Local::accept()
        {
            return 0;
        }

        // Construction
        Local::Local(Address * address, BaseSocket * _socket) : NameSocket(address), socket(*(_socket == 0 ? new BerkeleySocket() : _socket)), own(true) {}



/*
        // Open a socket of the specified type
        bool open(const Type type);
        // Close the socket
        bool close();
        // Accept a connection (and fill the address on success)
        BaseSocket * accept(Address *& address);

        // Bind a socket on the given address
        bool bind(const Address & address);
        // Connect a socket to the given address
        bool connect(const Address & address);
        // Listen on such socket
        bool listen(const int maxAllowedConnection);
        // Receive data on the socket
        int receive(char * buffer, const int bufferSize, const int flags) const;
        // Send data on the socket
        int send(const char * buffer, const int bufferSize, const int flags) const;
        // Set the socket option
        bool setOption(const Option option, const int value);
        // Select on this socket.
        bool select(const bool reading, const bool writing, const Time::TimeOut & timeout) const;
        // Append this socket to a monitoring pool (so it's possible to select over multiple socket later on)
        MonitoringPool * appendToMonitoringPool(MonitoringPool * pool) const;
        */



    }
}
