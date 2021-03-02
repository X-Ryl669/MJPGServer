#ifndef hpp_ThreadPoolPolicy_hpp
#define hpp_ThreadPoolPolicy_hpp

#ifndef InServerHPP
#error You must include Server.hpp and not this file directly
#endif

/** The thread pool policy use the server monitoring pool to select
    on non-blocking socket (on the current thread)

    A thread pool is then used to answer pending connection.

    The behavior is asynchronous (the system calls your callback from any
    thread context, you don't have to know which one).


    Each thread must implement a specific interface described below.
    The thread pool policy prefers answering connections (so it won't accept
    any new connection while the thread pool isn't ready to process new client)
    to accepting new connections.

    For performance reasons, the inheritance use template instead of virtual table.
    This avoid a pointer dereference for each operation.

    The base class (template argument) must have the following signature:
    @code
    class Base
    {
    protected:
        MonitoringPool  pool; // Server pool
        // Callback : A client has been rejected (no more space in pool)
        void rejectedNewClient(BaseSocket * client, Address * address);
        // Callback : A client has been accepted
        bool acceptedNewClient(BaseSocket * client, Address * address);
        // Callback : The given socket is now readable. You must read all available data from that socket
        //            If you want to send a file or some data, you can fill the appropriate parameters, the server will send them
        //            You can't use this possibility if you want to monitor write availability
        bool clientReadPossible(BaseSocket * client, String & prefixData, String & filePathToSend, String & suffixData, bool & closeSocketAfterSending);
        // Callback : The given socket is now writable.
        bool clientWritePossible(BaseSocket * client);
        // Callback : Client connection closed
        void clientConnectionClosed(BaseSocket * client);
        // Callback for customization : Return true if you want the server to monitor when a socket is writable
        bool wantToMonitorWrite();
        // Return true while the server is running
        bool isRunning() const;
    };
    @endcode
    @warning Usually, it's better to let the client pool "owns" its sockets.
    @warning Never, ever call a method from this class from a callback (as you'll probably crash or deadlock)

*/
template<class T>
struct ThreadPoolPolicy : public T
{
    // Type definition and enumeration
protected:
    /** The string we are using */
    typedef Strings::FastString String;
    /** Our InternalObject */
    class AsyncInternalObject : public InternalObject
    {
    public:
        String  filePath;
        /** Set the file to send */
        virtual bool setFileToSend(const File::Info & info)
        {
            if (!info.checkPermission(File::Info::Reading))
            {
                delete0(streamToSend);
                return false;
            }
            filePath = info.getFullPath();
            return true;
        }
        /** Set the stream to send */
        virtual bool setStreamToSend(::Stream::InputStream * inStream) { filePath = ""; return InternalObject::setStreamToSend(inStream); }
        /** Get the file path to send */
        virtual const String getFilePathToSend() const { return filePath; }
        /** Constructor */
        AsyncInternalObject() : InternalObject() {}
        /** Destructor */
        virtual ~AsyncInternalObject() {}
    };


    /** The possible error in this policy */
    enum ErrorCode
    {
        RemoveFailed    =   -2,     //!< The removing of a stream failed
        AppendFailed    =   -3,     //!< Appending the stream failed
    };
    /** The default timeout for our pool */
    enum { LowTimeout = 2, DefaultMaxClientPerThread = 200 };

    /** When the server loop accept new client, they are moved to a client thread.
        This thread understand the request, and if local IO are required, perform the
        waiting for IO correctly. */
    class ClientThread : public Threading::Thread, public Threading::WithStartMarker, public Network::Socket::BaseSocket::SDAFCallback
    {
        // Members
    private:
        /** A reference on the server */
        ThreadPoolPolicy &                                      server;
        /** The max socket count per thread */
        size_t                                                  maxClientPerThread;
        /** The monitoring pool for this thread */
        Network::Socket::MonitoringPool *                       pool;
        /** The monitoring pool for sending data */
        Network::Socket::MonitoringPool *                       send;
        /** The wake up event */
        Threading::Event                                        pendingListChanged;
        /** The items list that are waiting for to be moved into the work lists and to be managed by the client thread */
        Container::PlainOldData<BaseSocket *>::Array            pendingList;
        /** The Lock to protect pending list */
        Threading::Lock                                         lockPendingList;
        /** Auto reset event to notify a remove is asked */
        Threading::Event                                        poolAccessRequired;
        /** Auto reset event to notify the main loop is waiting for the remove execution */
        Threading::Event                                        mainLoopWaiting;
        /** Auto reset event to notify the remove has been done */
        Threading::Event                                        poolAccessDone;
        /** The items list were sent successfully and are waiting for to be moved into the work lists and to be managed by the client thread */
        Container::PlainOldData<BaseSocket *>::Array            socketSentDoneList;
        /** The Lock to protect pending list */
        Threading::Lock                                         lockSendDoneList;
        /** The wake up event */
        Threading::Event                                        sentDoneListChanged;
        /** The atomic socket count of the thread (including pool and pending list)
            This is used to speed up client processing in threads */
        uint32                                                  currentSocketCount;


        // Threading interface
    public:
        /** The monitoring pool thread*/
        virtual uint32 runThread()
        {
            started();
            while(isRunning())
            {
                // Stop the process on remove asked (classic flip flop signalling)
                if (poolAccessRequired.Wait(Threading::InstantCheck))
                {
                    mainLoopWaiting.Set();
                    poolAccessDone.Wait(Threading::Infinite);
                }
                // Check for any pending processing
                if ((!pool->getSize() && pendingListChanged.Wait(20)) )
                {
                    Threading::ScopedLock scope1(lockPendingList);
                    if(movePendingListIntoWorkingList())
                        pendingListChanged.Reset();
                }

                // Process the input socket loop
                if(pool->getSize())
                {
                    if (sentDoneListChanged.Wait((Threading::InstantCheck)))
                    {
                        Threading::ScopedLock scope(lockSendDoneList);
                        if(moveSendingDoneListIntoWorkingList())
                            sentDoneListChanged.Reset();
                    }
                    if (pendingListChanged.Wait((Threading::InstantCheck)))
                    {
                        Threading::ScopedLock scope(lockPendingList);
                        if(movePendingListIntoWorkingList())
                            pendingListChanged.Reset();
                    }

                    // Wait until there's data ready to read
                    if (pool->isReadPossible(LowTimeout / 2))
                    {
                        int index = -1;
                        while ((index = pool->getNextReadySocket(index)) != -1)
                        {
                            BaseSocket * socket = pool->getReadyAt(index);
                            // The buffer used to request is reset in this method
                            int res = server.socketReceiveReady(socket, this);
                            if (res == 0)
                            {
                                // Ok, need to remove the socket from the pool,
                                removeFromPools(socket);
                                break;
                            }
                            else if (res == -1)
                            {
                                forgetFromPools(socket);
                                break;
                            }
                            else
                            {
                                // If there is data to send to answer the request, remove the socket from the send pool.
                                // The sendDataAndFile has been asked in socketReceiveReady already, but is not finished.
                                // We must disable the socket for the other pool (send pool) to avoid a double sending
                                // At the end of the sending process, the socket will be automatically added to the send pool
                                InternalObject * intern = (InternalObject*)socket->getPrivateField();
                                if (intern && intern->hasDataToSend())
                                {
                                    // Remove socket from the send pool and not the other pool
                                    removeFromPools(socket, true);
                                    // No break here as we only have modified the send pool
                                }
                            }
                        }
                        // Done processing ready sockets
                    }
                }

                // Then process the output socket loop
                if (send->getSize())
                {
                    // If any sending is done, let's move them back to the main polling loop
                    if (sentDoneListChanged.Wait((Threading::InstantCheck)))
                    {
                        Threading::ScopedLock scope(lockSendDoneList);
                        if(moveSendingDoneListIntoWorkingList())
                            sentDoneListChanged.Reset();
                    }
                    // Redo operations to add any socket faster than one loop step
                    if (pendingListChanged.Wait((Threading::InstantCheck)))
                    {
                        Threading::ScopedLock scope(lockPendingList);
                        if(movePendingListIntoWorkingList())
                            pendingListChanged.Reset();
                    }

                    // Wait until there's data ready to send
                    if (send->isWritePossible(LowTimeout / 2))
                    {
                        int index = -1;
                        while ((index = send->getNextReadySocket(index)) != -1)
                        {
                            BaseSocket * socket = send->getReadyAt(index);
                            AsyncInternalObject *& intern = (AsyncInternalObject *&)socket->getPrivateField();
                            if (!server.socketSendReady(socket, intern))
                            {
                                removeFromPools(socket, true);
                                break;
                            }

                            // Interrogate the callback to send some additional values
                            server.doneSending(socket, intern);

                            // If there is any data to send now, please do
                            if(intern && intern->hasDataToSend())
                            {
                                // To avoid error on the sending order, at the end of send the socket will be added automatically to the list, so we avoid having it in the list too
                                removeFromPools(socket, true);
                                // Ok, the connection progressed, so let's touch it
                                intern->touchConnection();
                                if (!intern->getFilePathToSend() && intern->getStreamToSend())
                                {
                                    // We need to send a stream, and not a file this time, so proceed with a little help from Stream
                                    // While we are doing this, we are blocking the server so the performance will be very poor
                                    //@todo Move this to a sending thread that'll be called our server when done
                                    ::Stream::MemoryBlockStream suffixStream(intern->getSuffixBuffer().getBuffer(), intern->getSuffixBuffer().getSize()), prefixStream(intern->getPrefixBuffer().getBuffer(), intern->getPrefixBuffer().getSize());
                                    ::Stream::SuccessiveStream streamAndSuffix(*intern->getStreamToSend(), suffixStream);
                                    ::Stream::SuccessiveStream prefixAndTheRest(prefixStream, streamAndSuffix);
                                    ::Stream::SocketOutputStream outStream(*socket);
                                    if (!::Stream::copyStream(prefixAndTheRest, outStream))
                                    {
                                        server.closeClientSocket(socket);
                                        removeFromPools(socket);
                                        break;
                                    }
                                }
                                else if (!socket->sendDataAndFile((const char*)intern->getPrefixBuffer().getBuffer(), intern->getPrefixBuffer().getSize(), intern->getFilePathToSend(), 0, 0, (const char*)intern->getSuffixBuffer().getBuffer(), intern->getSuffixBuffer().getSize(), *this))
                                {
                                    server.closeClientSocket(socket);
                                    removeFromPools(socket);
                                    break;
                                }
                                // Reset the loop so it can continue
                                send->isWritePossible(0);
                                index = -1;
                            }
                        }

                        // Done write loop
                        server.writeLoopFinished();
                    }
                }
            }

            return 0;
        }
        // Helpers
    private:
        /** Remove socket from pool */
        void removeFromPools(BaseSocket * socket, const bool sendOnly = false) volatile
        {
            // First remove from send pool (we don't check the return value as the socket might not be inside the send pool anyway)
            send->removeSocket(socket);
            if(!sendOnly)
            {
                if(!pool->removeSocket(socket))
                    server.signalError(RemoveFailed, socket);
                else
                {
                    Threading::SharedDataReaderWriter sdw(currentSocketCount);
                    sdw--;
                }
            }
        }
        /** Remove socket from pool */
        void forgetFromPools(BaseSocket * socket, const bool sendOnly = false) volatile
        {
            // First remove from send pool (we don't check the return value as the socket might not be inside the send pool anyway)
            send->forgetSocket(socket);
            if(!sendOnly)
            {
                if(!pool->forgetSocket(socket))
                    server.signalError(RemoveFailed, socket);
                else
                {
                    Threading::SharedDataReaderWriter sdw(currentSocketCount);
                    sdw--;
                }
            }
        }
        /** Move the socket from the pending list into the working list */
        bool movePendingListIntoWorkingList()
        {
            // Move the items from pendingList to workList
            while(pendingList.getSize() > 0)
            {
                BaseSocket * currentSocket = pendingList[0];
                if(currentSocket)
                {
                    // Copy address and pool list will delete the memory (owned socket)
                    if(!pool->appendSocket(currentSocket))
                    {
                        // At this step, no data sent so no cancellation of data sent so no deadlock
                        server.closeClientSocket(currentSocket);
                        pendingList.Remove(0);
                        delete currentSocket;
                        return false;
                    }
                    if(!send->appendSocket(currentSocket))
                    {
                        // At this step, no data sent so no cancellation of data sent so no deadlock
                        server.closeClientSocket(currentSocket);
                        pendingList.Remove(0);
                        // Remove from the other pool and free memory
                        removeFromPools(currentSocket);
                        return false;
                    }
                }
                pendingList.Remove(0);
            }
            // The pending list is clear
            pendingList.Clear();
            return true;
        }
        /** Move the socket from the pending list into the working list */
        bool moveSendingDoneListIntoWorkingList()
        {
            // Move the items from pendingList to workList
            while(socketSentDoneList.getSize() > 0)
            {
                BaseSocket * socket = socketSentDoneList[0];
                if(socket)
                {
                    // check if the socket is still alive before changing it
                    if(pool->haveSocket(socket))
                    {
                        // Touch the intern object
                        AsyncInternalObject * intern = (AsyncInternalObject *)socket->getPrivateField();
                        bool mustClose = false;
                        if (intern)
                        {
                            mustClose = intern->shouldCloseConnectionOnReply();
                            // Simply empty the intern object
                            intern->getPrefixBuffer().Extract(0, intern->getPrefixBuffer().getSize());
                            intern->setFileToSend(String(""));
                            intern->getSuffixBuffer().Extract(0, intern->getSuffixBuffer().getSize());
                        }
                        // The connection must continue so we reset the buffer after the answer
                        if((intern && intern->shouldResetAfterSending()))
                        {
                            // Reset doesn't clean the private field
                            intern->Reset();
                            server.deletePrivateField(intern->getPrivateField());
                        }
                        if(mustClose)
                        {
                            if(intern)
                            {
                                // We need to close the socket to avoid a memory leak
                                intern->Reset();
                                server.deletePrivateField(intern->getPrivateField());
                                // We are here because the send has been done or has been canceled
                                server.closeClientSocket(socket);
                            }
                            removeFromPools(socket);
                        }
                        else if (!send->haveSocket(socket) && !send->appendSocket(socket))
                            return false;
                    }
                }
                socketSentDoneList.Remove(0);
            }
            // The pending list is clear
            socketSentDoneList.Clear();
            return true;
        }
        /** Remove all the streams from the pool.
            Can be called by other threads than client thread */
        bool removeAllSockets()
        {
            Threading::SharedDataReaderWriter sdw(currentSocketCount);
            {
                Threading::ScopedLock scope2(lockPendingList);
                while(pendingList.getSize() > 0)
                {
                    BaseSocket * currentSocket = pendingList[0];
                    if(currentSocket)
                    {
                        server.closeClientSocket(currentSocket);
                        pendingList.Remove(0);
                        delete currentSocket;
                        sdw--;
                    }
                }
                // The pending list is clear
                pendingList.Clear();
            }
            poolAccessRequired.Set();
            // We wait the main loop stop itself with no lock taken
            while (!mainLoopWaiting.Wait(20) && isRunning());

            while (pool->getSize())
            {
                // Should check for asynchronous socket sending, and stop them now

                // It's not important to call this method twice, but it's utterly important to
                // let the client know that we are closing its socket
                BaseSocket * socket = (*pool)[0];
                server.closeClientSocket(socket);
                send->removeSocket(socket);
                if (!pool->removeSocket(socket))
                {
                    // Free the main loop
                    poolAccessDone.Set();
                    return false;
                }
                else sdw--;
            }
            // Free the main loop
            poolAccessDone.Set();
            // No need to remove socket from the send pool because the socket have already been deleted
            // The sent pool is a list that doens't own socket
            return true;
        }


        // Multi threading pool interface
    public:
        /** Called when the stream is ready.
            @warning This is called asynchronously from another (kernel) thread, so you can't rely on any locking order.  */
        void finishedSending(BaseSocket * socket, const uint64 sentSize)
        {
            if (socket)
            {
                socket->setOption(Network::Socket::BaseSocket::Blocking, 1);
                Threading::ScopedLock scope(lockSendDoneList);
                socketSentDoneList.Append(socket);
                sentDoneListChanged.Set();
            }
        }
        /** Start the waiting pool */
        inline bool startMonitoring()
        {
            if (!createThread())
                return false;
            return waitUntilStarted();
        }
        /** Stop the waiting pool */
        inline bool stopMonitoring()
        {
            bool ret = removeAllSockets();
            return destroyThread() ? ret : false;
        }

        /** Append the socket to the thread. This socket is stored into a pending list to avoid too many synchronizations penalty
            @param socket  The socket to append
            @return true if the socket will be managed by this thread
        */
        inline bool canHandleClient(BaseSocket * socket)
        {
            // If main thread is moving previous values of pending list, the event cannot be set before the previous reset of the event
            Threading::ScopedLock scope2(lockPendingList);
            Threading::SharedDataReaderWriter sdw(currentSocketCount);
            if (sdw < maxClientPerThread)
            {
                sdw++;
                pendingList.Append(socket);
                pendingListChanged.Set();
                return true;
            }
            return false;
        }

    public:
        /** Build the object */
        ClientThread(ThreadPoolPolicy & server, size_t maxSocketPerThread = DefaultMaxClientPerThread, Network::Socket::MonitoringPool * pool = 0, Network::Socket::MonitoringPool * send = 0) :  server(server), maxClientPerThread(maxSocketPerThread), pool(pool == 0 ? new Network::Socket::FastBerkeleyPool(true) : pool), send(send == 0 ? new Network::Socket::FastBerkeleyPool(false) : send),
            pendingListChanged("Wake up"), lockPendingList("lockPendingList"), poolAccessRequired("poolAccessRequired", Threading::Event::AutoReset), mainLoopWaiting("mainLoopWaiting", Threading::Event::AutoReset), poolAccessDone("poolAccessDone", Threading::Event::AutoReset), lockSendDoneList("lockSendDoneList"), sentDoneListChanged("sentDoneListChanged"), currentSocketCount(0)
        {}
        ~ClientThread()
        {
            stopMonitoring();
            {
                Threading::ScopedLock scope(lockPendingList);
                while(pendingList.getSize() > 0)
                {
                    BaseSocket * currentSocket = pendingList[0];
                    if(currentSocket)
                    {
                        pendingList.Remove(0);
                        delete currentSocket;
                    }
                }
            }
            delete pool;
            delete send;
        }
    };
    friend class ClientThread;


    /** The socket mapping structure */
    typedef Container::HashTable<ClientThread, uint64,
                                    Container::NoHashKey<uint64>,
                                    Container::NoDeletion<ClientThread>, false>  MapSocketClientT;
    /** The client thread array */
    typedef typename Container::NotConstructible<ClientThread>::IndexList        ClientArrayT;
    /** The empty client thread request list */
    typedef typename Container::PlainOldData<ClientThread *>::Array               RequireCleaningT;


private:
    /** The max socket count per thread */
    size_t                          maxClientPerThread;
    /** The thread pool */
    ClientArrayT                    clientArray;
    /** The client thread that needs cleaning */
    RequireCleaningT                requireCleaning;
    /** The cleaning thread lock */
    Threading::Lock                 cleaningLock;

    // Our send error code
    enum SendError
    {
        SocketIsDead     = -1,
        SocketIsBlocking = 1,
        FileIsBlocking   = 0,
        SocketIsDone     = 0,
        FileIsDone       = 0,
    };

    // Internal interface
private:

    /** We are done sending our stuff, let the child send his */
    inline bool doneSending(BaseSocket * socket, AsyncInternalObject * intern)
    {
        if (this->wantToMonitorWrite() && !this->clientWritePossible(*socket, *intern))
            return false;
        // If we don't want to monitor write, let's remove this socket from the pool
        return this->wantToMonitorWrite();
    }
    /** Called when the socket is ready to send.
        @return true when you're done with the socket */
    inline bool socketSendReady(BaseSocket * socket, AsyncInternalObject * intern)
    {
        // Read from the socket
        if (!intern || intern->shouldBeCleaned(this->maxLingerTime()))
        {   // Bad object, let's remove it
            closeClientSocket(socket);
            return false;
        }
        return true;
    }

    /** Called when the socket is ready to send.
        @return 1 when you're done with the socket, 0 when you want the socket to be removed from the pools, -1 when you want it forgot from the pools */
    inline int socketReceiveReady(BaseSocket * socket, ClientThread * thread)
    {
        // Read from the socket
        AsyncInternalObject *& intern = (AsyncInternalObject *&)socket->getPrivateField();
        if (!intern || intern->shouldBeCleaned(this->maxLingerTime()))
        {   // Bad object, let's remove it
            closeClientSocket(socket);
            return 0;
        }

        // Can't receive for now, until all data isn't sent
        if (intern->hasDataToSend()) return 1;

        uint32 minRecvSize = this->minimumAmountToRead();
        // Ok, make progress in receiving
        if (!receiveAsMuchAsPossible(socket, intern))
            return 0;

        if (intern->getRecvBuffer().getSize() > minRecvSize)
        {   // Have enough data to tell the client ?
            if (!this->clientReadPossible(*socket, *intern))
            {   // Ok, close the connection
                closeClientSocket(socket);
                return 0;
            }
            // Check if we need to forget this socket
            if (intern->shouldForgetSocket())
            {
                // That's the only case where we send data in the receiving part, since after forgetting, there is no link to the socket any more
                if (intern->hasDataToSend())
                    sendPendingData(socket, intern);
                forgetClientSocket(socket);
                return -1;
            }
            // Ok, the connection progressed, so let's touch it
            intern->touchConnection();

            // Check if we have a file to send and in that case, use OS's send_file method, or default to IO vector sending
            if (intern->getFilePathToSend())
            {
                if (!socket->sendDataAndFile((const char*)intern->getPrefixBuffer().getBuffer(), intern->getPrefixBuffer().getSize(), intern->getFilePathToSend(), 0, 0, (const char*)intern->getSuffixBuffer().getBuffer(), intern->getSuffixBuffer().getSize(), *thread))
                {
                    closeClientSocket(socket);
                    return 0;
                }
            } else // Use IO vector
            {
                if (intern->getStreamToSend())
                {
                    // We need to send a stream, and not a file this time, so proceed with a little help from Stream
                    // While we are doing this, we are blocking the server so the performance will be very poor
                    //@todo Move this to a sending thread that'll be called our server when done
                    ::Stream::MemoryBlockStream suffixStream(intern->getSuffixBuffer().getBuffer(), intern->getSuffixBuffer().getSize()), prefixStream(intern->getPrefixBuffer().getBuffer(), intern->getPrefixBuffer().getSize());
                    ::Stream::SuccessiveStream streamAndSuffix(*intern->getStreamToSend(), suffixStream);
                    ::Stream::SuccessiveStream prefixAndTheRest(prefixStream, streamAndSuffix);
                    ::Stream::SocketOutputStream outStream(*socket);
                    if (!::Stream::copyStream(prefixAndTheRest, outStream))
                    {
                        closeClientSocket(socket);
                        return 0;
                    }
                }
                else
                {
                    const char * buffers[2] = { (const char*)intern->getPrefixBuffer().getBuffer(), (const char*)intern->getSuffixBuffer().getBuffer() };
                    const int buffersSize[2] = { (int)intern->getPrefixBuffer().getSize(),  (int)intern->getSuffixBuffer().getSize() };
                    if (socket->sendBuffers(buffers, buffersSize, 2) != buffersSize[0] + buffersSize[1])
                    {
                        closeClientSocket(socket);
                        return 0;
                    }
                }
            }
            if (intern->shouldResetAfterSending()) intern->Reset();
        }
        return 1;
    }
    /** Ok, send the programmed data now */
    SendError sendPendingData(BaseSocket * socket, InternalObject * intern)
    {
        if (!socket || !intern) return SocketIsDead;
        // Send the initial data if any
        if (intern->getPrefixBuffer().getSize())
        {
            uint8 * buffer = intern->getPrefixBuffer().getBuffer();
            uint32 sentSize = 0;
            while (sentSize < intern->getPrefixBuffer().getSize())
            {
                int sent = socket->send((const char*)&buffer[sentSize], (int)(intern->getPrefixBuffer().getSize() - sentSize), 0);
                if (sent == 0) return SocketIsDead;
                if (sent < 0)
                {
                    if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                    {
                        intern->getPrefixBuffer().Extract(0, sentSize);
                        return SocketIsBlocking;
                    }
                    return SocketIsDead;
                }
                sentSize += (uint32)sent;
            }
            intern->getPrefixBuffer().Extract(0, sentSize);
        }
        // Send the file
        if (intern->getStreamToSend())
        {
            Stream::InputStream * stream = intern->getStreamToSend();
            uint64 fileSize = stream->fullSize();
            char buffer[4096];
            uint64 currentPos = stream->currentPosition();
            while (currentPos < fileSize)
            {
                int ret = (int)stream->read(buffer, (int)min((uint64)4096, (fileSize - currentPos)));
                if (ret <= 0)
                {
#if WantAsyncFile == 1
                    if (ret != File::AsyncStream::Asynchronous)
                    {
                        intern->setFileToSend("");
                        return SocketIsDead;
                    }
#endif
                    // Done, for the asynchronous stream (will be removed from the send pool, and added back as soon as the file is ready again)
                    return FileIsBlocking;
                }
                currentPos += ret;
                int sentSize = 0;
                while (sentSize < ret)
                {
                    int sent = socket->send(&buffer[sentSize], (int)(ret - sentSize), 0);
                    if (sent == 0) return SocketIsDead;
                    if (sent < 0)
                    {
                        if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                        {
                            // Would block, we simple put the read file buffer in memory so it'll be sent next iteration
                            if (intern->getPrefixBuffer().Append((const uint8*)&buffer[sentSize], (uint32)(ret - sentSize)))
                                return SocketIsDead;
                            return SocketIsBlocking;
                        }
                        return SocketIsDead;
                    }
                    sentSize += sent;
                }
            }
            return FileIsDone;
        }
        // Send the suffix data if any
        if (intern->getSuffixBuffer().getSize())
        {
            uint8 * buffer = intern->getSuffixBuffer().getBuffer();
            uint32 sentSize = 0;
            while (sentSize < intern->getSuffixBuffer().getSize())
            {
                int sent = socket->send((const char*)&buffer[sentSize], (int)(intern->getSuffixBuffer().getSize() - sentSize), 0);
                if (sent == 0) return SocketIsDead;
                if (sent < 0)
                {
                    if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                    {
                        intern->getSuffixBuffer().Extract(0, sentSize);
                        return SocketIsBlocking;
                    }
                    return SocketIsDead;
                }
                sentSize += (uint32)sent;
            }
            intern->getSuffixBuffer().Extract(0, sentSize);
        }
        return intern->shouldCloseAfterSending() ? SocketIsDead : SocketIsDone;
    }

    /** No more socket in this client thread, so will be cleaned at next iteration */
    inline void noMoreSocket(ClientThread * thread)
    {
        if (!thread) return;
        Threading::ScopedLock scope(cleaningLock);
        requireCleaning.Append(thread);
    }
    /** Close a client socket always called by client thread */
    inline void closeClientSocket(BaseSocket * socket)
    {
        AsyncInternalObject *& intern = (AsyncInternalObject *&)socket->getPrivateField();
        if (intern)
        {
            this->clientConnectionClosed(*socket, *intern);
            // Then clean the asynchronous file
            if (intern->hasDataToSend())
                socket->cancelAsyncSend();
            delete0(intern);
            socket->getPrivateField() = 0;
        }
    }
    /** Forget a client socket always called by client thread */
    inline void forgetClientSocket(BaseSocket * socket)
    {
        AsyncInternalObject *& intern = (AsyncInternalObject *&)socket->getPrivateField();
        if (intern)
        {
            // Then clean the asynchronous file
            if (intern->hasDataToSend())
                socket->cancelAsyncSend();
            delete0(intern);
            socket->getPrivateField() = 0;
        }
    }
    /** Receive as much as possible with non blocking semantic */
    bool receiveAsMuchAsPossible(BaseSocket * socket, InternalObject * intern)
    {
        char Buffer[4096];
        while (true)
        {
            int received = socket->receive(Buffer, ArrSz(Buffer), 0);
            if (received == 0)
            {
                // Let the server know we have received data anyway
                if (intern->getRecvBuffer().getSize())
                    this->clientReadPossible(*socket, *intern);

                closeClientSocket(socket);
                return false;
            }
            if (received > 0)
                intern->getRecvBuffer().Append((uint8*)Buffer, received);
            if (received == (int)ArrSz(Buffer)) continue;
            return (received > 0 && received < (int)ArrSz(Buffer)) || (received < 0 && socket->getLastError() == Network::Socket::BaseSocket::InProgress);
        }
    }

    // Public interface
public:
    /** Signal an error */
    inline void signalError(const ErrorCode code, void * obj) { /* static_cast<const T*>(this)->signalError(code, obj); */ }

    /** Check if the server is still running */
    inline bool isRunning() const { return static_cast<const T*>(this)->isRunning(); }

    /** Start the server (you must have set the port & address to bind on) */
    inline bool startServer()
    {
        if (!this->pool.getSize()) return false;

        for (uint32 i = 0; i < this->pool.getSize(); i++)
        {   // Set socket as non blocking
            BaseSocket * socket = this->pool[i];
            if (socket)
            {
                // Make sure the server socket is correctly set up
                if (socket->getState() < Network::Socket::BaseSocket::Bound) return false;
                if (socket->getState() < Network::Socket::BaseSocket::Listening
                    && socket->getType() == Network::Socket::BaseSocket::Stream
                    && socket->listen() != Network::Socket::BaseSocket::Success) return false;

                socket->setOption(Network::Socket::BaseSocket::Blocking, 0);
            }
        }
#ifdef _LINUX
        struct sigaction actual;
        if (sigaction(SIGPIPE, (struct sigaction *)NULL, &actual) != 0) return false;
        if (actual.sa_handler == SIG_DFL)
        {   // Ignore default action on SIGPIPE (avoid being killed if socket is closed on other side)
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

        // Check the pool if any accepting event occurred
        if (!this->pool.getSize()) return false;
        Network::Socket::MonitoringPool & server = this->pool;

        {
            Threading::ScopedLock scope(cleaningLock);
            for (size_t i = requireCleaning.getSize(); i > 0; i--)
            {
                ClientThread * thread = requireCleaning[(uint32)i - 1];
                if (thread)
                {
                    // Need to find the thread in the client array
                    size_t pos = clientArray.indexOf(thread);
                    if (pos != clientArray.getSize() && pos) // We never remove the first client
                    {
                        thread->stopMonitoring();
                        clientArray.Remove((uint32)pos);
                    }
                }
                requireCleaning.Remove((uint32)i - 1);
            }
        }

        if (server.isReadPossible(timeout))
        {
            // Then accept the new connections
            uint32 clientIndex = server.getNextReadySocket(-1);
            while (clientIndex != -1)
            {
                BaseSocket * socket = server.getReadyAt(clientIndex);
                if (socket)
                {
                    Utils::ScopePtr<BaseAddress> address = 0;
                    Utils::ScopePtr<BaseSocket> client(socket->accept((BaseAddress *&)address));
                    // Let auto clean the address on scope exit
                    Utils::ScopePtr<AsyncInternalObject> newField(new AsyncInternalObject);
                    if (client)
                    {
                        client->getPrivateField() = (void *)(AsyncInternalObject*)newField;
                        client->setOption(Network::Socket::BaseSocket::Blocking, 0);
                        // Tell the callback that the client was accepted
                        if (!this->acceptedNewClient(*client, *address))
                        {
                            this->rejectedNewClient(*client, *address);
                            clientIndex = server.getNextReadySocket(clientIndex);
                            continue;
                        }

                        // Find a free client thread to accept this client
                        int i = (int)clientArray.getSize() - 1;
                        for (; i >= 0; i--)
                        {
                            ClientThread * thread = clientArray.getElementAtUncheckedPosition((uint32)i);
                            // Memory error
                            if (!thread)
                            {
                                this->rejectedNewClient(*client, *address);
                                return false;
                            }
                            if(thread->canHandleClient(client))
                                break;
                        }
                        if (i < 0)
                        {   // Second try, this times, we might block a little
                            // (we are using random position in the set so the first client isn't necessary the most loaded)
                            int tries = (int)min((uint32)3, (uint32)clientArray.getSize());
                            while (tries)
                            {
                                i = Random::numberBetween(0, (uint32)clientArray.getSize() - 1);
                                ClientThread * thread = clientArray.getElementAtUncheckedPosition((uint32)i);
                                if(thread->canHandleClient(client))
                                    break;
                                tries--;
                            }

                            // No possible to fit this client, so let's create another client thread now
                            if (tries == 0)
                            {
                                ClientThread * thread = new ClientThread(*this, maxClientPerThread);
                                if (!thread)
                                {
                                    this->rejectedNewClient(*client, *address);
                                    return false;
                                }
                                // A new thread must always accept at least the first client
                                if(!thread->canHandleClient(client))
                                {
                                    // Can't accept this client
                                    this->rejectedNewClient(*client, *address);
                                    delete thread;
                                    return false;
                                }
                                // Ok, great, let's append the thread to our pool
                                clientArray.Append(thread);

                                // Then start the thread
                                if (!thread->startMonitoring())
                                {
                                    // The thread is now the owner of the socket : must not be deleted
                                    (void)client.Forget(); (void)newField.Forget();
                                    // The thread will be deleted in the clientArray purge
                                    return false;
                                }
                            }
                        }
                        // It worked so don't delete this client (as it's owned by a thread now)
                        (void)client.Forget(); (void)newField.Forget();
                    }
                }
                clientIndex = server.getNextReadySocket(clientIndex);
            }
        }
        return true;
    }

    /** Stop the server */
    inline bool stopServer()
    {
        this->serverStopping();

        if (!this->pool.getSize()) return false;
        clientArray.Clear();

        for (uint32 i = 0; i < this->pool.getSize(); i++)
        {   // Set socket as non blocking
            BaseSocket * socket = this->pool[i];
            if (socket)
                socket->setOption(Network::Socket::BaseSocket::Blocking, 1);
        }
        return true;
    }

    /** The constructor */
    ThreadPoolPolicy(Network::Socket::MonitoringPool * serverPool, size_t maxSocketPerThread = DefaultMaxClientPerThread) : T(serverPool), maxClientPerThread(maxSocketPerThread) {}
    /** Any additional server parameters can be passed to this template method
        @param serverPool   The pool of server socket to monitor
        @param clientPool   The pool of client socket to monitor (usually empty unless you're bootstrapping a running server)
        @param arg          This is an additional parameter that's passed to the Server constructor (in addition to the server pool) */
    template <class Y>
    ThreadPoolPolicy(Network::Socket::MonitoringPool * serverPool, Y & arg, size_t maxSocketPerThread = DefaultMaxClientPerThread) : T(arg, serverPool), maxClientPerThread(maxSocketPerThread) {}
    /** The destructor */
    virtual ~ThreadPoolPolicy() {}
};

#endif
