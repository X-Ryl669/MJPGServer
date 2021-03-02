// We need our declaration
#include "../../include/Network/Server.hpp"
// We need the string hash function
#include "../../include/Hash/StringHash.hpp"
// We need utils
#include "../../include/Utils/ScopeGuard.hpp"
// We need textual headers too
#include "../../include/Network/Clients/TextualHeaders.hpp"

namespace Network
{
    namespace Server
    {
        InternalObject::InternalObject()
            : streamToSend(0), closeAfterSend(false), firstTime((int)time(NULL)), priv(0), resetAfterSend(false), clientAskedConnectionClose(false), forgetSocket(false), corkSocket(false), sentSize(0), fullSize(0), tcpMSS(0) {}
        void InternalObject::Reset()
        {
            resetAfterSend = clientAskedConnectionClose = corkSocket = false;
            getPrefixBuffer().stripTo(0);
            getRecvBuffer().stripTo(0);
            getSuffixBuffer().stripTo(0);
            delete0(streamToSend);
            sentSize = 0; tcpMSS = 0;
        }
        void InternalObject::touchConnection() { firstTime = (int)time(NULL); }
        bool InternalObject::shouldBeCleaned(const int maxTimeoutInSec) const { return (time(NULL) - (time_t)firstTime) >= maxTimeoutInSec; }
        bool InternalObject::setFileToSend(const File::Info & info)
        {
#if !defined(NO_FILES_ON_PLATFORM)
            return false;
#else
            if (!info.checkPermission(File::Info::Reading))
            {
                delete0(streamToSend);
                return false;
            }
            streamToSend = new Stream::InputFileStream(info.getFullPath());
            return streamToSend != 0;
#endif
        }
        bool InternalObject::setFileToSend(const String & fullPath) { return setFileToSend(File::Info(fullPath)); }
        bool InternalObject::setStreamToSend(::Stream::InputStream * inStream) { delete0(streamToSend); streamToSend = inStream; return true; }
        bool InternalObject::hasDataToSend() const
        {
            return prefixBuffer.getSize() || suffixBuffer.getSize() || (streamToSend ? streamToSend->fullSize() : false);
        }
        InternalObject::~InternalObject() { delete0(streamToSend); }

        // Returns 0 on error, 1 on success, -1 if it blocked
        int sendPendingDataImpl(BaseSocket * socket, InternalObject * intern)
        {
            if (!socket || !intern) return 0;
            // Send the initial data if any
            if (intern->getPrefixBuffer().getSize())
            {
                if (intern->shouldCork())
                {
                    int mss = 0;
                    if (socket->getOption(BaseSocket::TCPMaxSeg, mss)) intern->setCorkLimit(mss);
                    intern->computeFullSize();
                    if (intern->getFullSize() < (uint64)mss) intern->setCorkLimit(0);
                    else socket->setOption(BaseSocket::Cork, 1);
                }
                uint8 * buffer = intern->getPrefixBuffer().getBuffer();
                uint32 sentSize = 0;
                while (sentSize < intern->getPrefixBuffer().getSize())
                {
                    int toSend = min((int)(intern->getPrefixBuffer().getSize() - sentSize), (int)intern->corkRemaining());
                    int sent = socket->send((const char*)&buffer[sentSize], toSend, 0);
                    if (sent == 0) return 0;
                    if (sent < 0)
                    {
                        if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                        {
                            intern->getPrefixBuffer().Extract(0, sentSize);
                            return -1;
                        }
                        return 0;
                    }
                    sentSize += (uint32)sent;
                    intern->sentBytes(sent);
                    if (intern->shouldCork() && !intern->corkRemaining()) { socket->setOption(BaseSocket::Cork, 0); intern->setCorkLimit(0); }
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
                    if (ret <= 0) return intern->setFileToSend("") ? 1 : 0;
                    currentPos += ret;
                    int sentSize = 0;
                    while (sentSize < ret)
                    {
                        int toSend = min((int)(ret - sentSize), (int)intern->corkRemaining());
                        int sent = socket->send(&buffer[sentSize], toSend, 0);
                        if (sent == 0) return intern->setFileToSend("") ? 1 : 0;
                        if (sent < 0)
                        {
                            if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                            {
                                // Would block, let rewind the file a bit
                                if (!stream->setPosition(currentPos - (ret - sentSize)))
                                    return intern->setFileToSend("") && 0;
                                return -1;
                            }
                            intern->setFileToSend("");
                            return 0;
                        }
                        sentSize += sent;
                        intern->sentBytes(sent);
                        if (intern->shouldCork() && !intern->corkRemaining()) { socket->setOption(BaseSocket::Cork, 0); intern->setCorkLimit(0); }
                    }
                }
                intern->setFileToSend("");
            }
            // Send the suffix data if any
            if (intern->getSuffixBuffer().getSize())
            {
                uint8 * buffer = intern->getSuffixBuffer().getBuffer();
                uint32 sentSize = 0;
                while (sentSize < intern->getSuffixBuffer().getSize())
                {
                    int toSend = min((int)(intern->getSuffixBuffer().getSize() - sentSize), (int)intern->corkRemaining());
                    int sent = socket->send((const char*)&buffer[sentSize], toSend, 0);
                    if (sent == 0) return 0;
                    if (sent < 0)
                    {
                        if (socket->getLastError() == Network::Socket::BaseSocket::InProgress)
                        {
                            intern->getSuffixBuffer().Extract(0, sentSize);
                            return -1;
                        }
                        return 0;
                    }
                    sentSize += (uint32)sent;
                    intern->sentBytes(sent);
                    if (intern->shouldCork() && !intern->corkRemaining()) { socket->setOption(BaseSocket::Cork, 0); intern->setCorkLimit(0); }
                }
                intern->getSuffixBuffer().Extract(0, sentSize);
            }
            return !intern->shouldCloseAfterSending() && !intern->shouldCloseConnectionOnReply() ? 1 : 0;
        }
        
        // Parse the Request line
        bool TextualHeadersServer::parseRequestLine(InternalObject & intern, const String & protocol, String line)
        {
            if (!getContext(intern)) return false;
            Context & context = *getContext(intern);
            const String & method = line.splitFrom(" ");
            if (!method || !addQueryHeader(context, "##METHOD##", method) || !isMethodSupported(method)) return false;
            
            const String & resource = line.splitFrom(" ");
            if (!resource) return false;
            
            String version = line.Trimmed();
            if (!version || version.splitAt(protocol.getLength()) != protocol) return false;
            if (!addQueryHeader(context, "##VERSION##", version.midString(0, version.getLength()))) return false;
            
            // Explode the resource in part
            Network::Address::URL url(resource);
            String Path = url.getPath(), Query = Network::Address::URL::unescapedURI(url.getQuery());
            if (Path.getLength() &&  Path[0] == '/') Path.leftTrim('/');
            if (!addQueryHeader(context, "##RESOURCE##", Path)) return false;
            if (!addQueryHeader(context, "##URL##", resource)) return false;
            
            if (Query)
            {
               // Extract any parameters out of the query
                String parameterName = Query.splitFrom("=");
                while (parameterName)
                {
                    String parameterValue = Query.splitUpTo("&");
                    if (!addQueryHeader(context, parameterName, parameterValue)) return false;
                    parameterName = Query.splitFrom("=");
                }
                // Add remaining if any left
                if (Query && !addQueryHeader(context, Query, "")) return false;
            }
            return true;
        }
        
        // Add a header to the query
        bool TextualHeadersServer::addQueryHeader(Context & context, const String & header, const String & value)
        {
            return context.query.storeValue(header, new String(value), true);
        }

        // Add a header to the answer 
        bool TextualHeadersServer::addAnswerHeader(Context & context, const String & header, const String & value)
        {
            return context.answer.storeValue(header, new String(value), true);
        }

        struct Merger 
        { 
            String & output;

            int MergeHeader(const String & key, const String * t) 
            {
                if (key)
                {
                    output += key;
                    output += ":";
                    if (t) output += *t;
                    output += "\r\n";
                }
                else
                    // First header is the answer line, so let's put it at the very top
                    if (t) output = *t + "\r\n" + output;
                return 1;
            }
            Merger(String & output) : output(output) {}
        };

        // Merge all headers to data 
        bool TextualHeadersServer::mergeAnswerHeaders(InternalObject & intern)
        {
            Context & context = *(Context*)intern.getPrivateField();
            String output;
            Merger merge(output);
            context.answer.iterateAllEntries(merge, &Merger::MergeHeader);
            if (output.getLength() == 0) return false;
            output += "\r\n";
            intern.getPrefixBuffer().Append(output, output.getLength());
            return true;
        }
        
        // Parsing headers method definition
        bool TextualHeadersServer::parseHeader(InternalObject & intern, String headerLine)
        {
            Context & context = *(Context*)intern.getPrivateField();
            String headerName = headerLine.splitFrom(':');
            if (headerName)
            {
                headerName = Client::TextualHeaders::fixHeaderName(headerName.Trimmed());
                headerLine.Trim();
                if (!addQueryHeader(context, headerName, headerLine)) return false;
                if (headerName == "Content-Type" && headerLine.Find("multipart") != -1)
                {
                    String boundary = headerLine.fromFirst("boundary").Trimmed(" =\"\r\n");
                    // Now we have the boundary, so save it
                    int boundaryLen = min(boundary.getLength(), 70);
                    if (!addQueryHeader(context, "##BOUNDARY##", String(boundaryLen, (const char*)boundary))) return false; 
                }
            }
            return true;
        }
    }
}

