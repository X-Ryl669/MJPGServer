// We need our declaration
#include "../../../include/Network/Servers/HTTP.hpp"
// We need streams too
#include "../../../include/Streams/SocketStream.hpp"
// And scoped pointer too
#include "../../../include/Utils/ScopePtr.hpp"


namespace Network
{
    namespace Server
    {
        String HTTP::getResource(const String & requestedResource)
        {
#if defined(NO_FILES_ON_PLATFORM)
            return requestedResource;
#else
            return File::General::normalizePath(File::General::getSpecialPath(File::General::Current) + PathSeparator + requestedResource).normalizedPath(Platform::Separator, false);
#endif
        }

        // Parse a client request.
        HTTP::ParsingError HTTP::parseRequest(InternalObject & intern, const BaseSocket & client)
        {
            Platform::EndOfLine EOLType = Platform::CRLF;
            // Try to find the request end
            if (intern.getRecvBuffer().lookFor((uint8*)"\r\n\r\n", 4) == (uint32)-1)
            {
                // Check for \n\n end of request
                if (intern.getRecvBuffer().lookFor((uint8*)"\n\n", 2) == (uint32)-1)
                {
                    // Check for \r\r end of request
                    if (intern.getRecvBuffer().lookFor((uint8*)"\r\r", 2) == (uint32)-1)
                        return intern.getRecvBuffer().getSize() < 4096 ? NotEnoughData : BadRequest;
                    EOLType = Platform::CR;
                } else EOLType = Platform::LF;
            } else EOLType = Platform::CRLF;

            // Request is complete
            // Ok, then parse each line (first line is a bit different)

            // Set the line separator
            const uint8 * lineSep = EOLType == Platform::LF ? (const uint8*)"\n" : (EOLType == Platform::CR ? (const uint8*)"\r" : (const uint8*)"\r\n");
            const uint32 lineSepSize = EOLType == Platform::CRLF ? 2 : 1;
            if (intern.getRecvBuffer().lookFor(lineSep, lineSepSize) == 0)
            {
                // Empty line, probably between a Keep Alive connection, so let's accept this any way
                intern.getRecvBuffer().Extract(0, lineSepSize);
            }

            uint32 currentPos = 0, firstLineEndPos = intern.getRecvBuffer().lookFor(lineSep, lineSepSize);

            // Ok, now parse the request line
            String requestLine(intern.getRecvBuffer().getBuffer(), firstLineEndPos);
            Context & context = *(Context *)intern.getPrivateField();
            if (!addQueryHeader(context, "##REQUEST##", requestLine)) return BadRequest;
            if (!parseRequestLine(intern, "HTTP/", requestLine) || !isMethodSupported(*context.query.getValue("##METHOD##"))) return BadRequest;
            String * value = context.query.getValue("##RESOURCE##");
            Logger::log(Logger::Dump, "Client asked for %s", value ? (const char*)*value : (const char*)requestLine);

            // Parse all remaining headers
            currentPos = firstLineEndPos + lineSepSize;
            while (currentPos < intern.getRecvBuffer().getSize())
            {
                uint32 endLinePos = intern.getRecvBuffer().lookFor(lineSep, lineSepSize, currentPos);
                if (endLinePos - currentPos < 3) { currentPos = endLinePos + lineSepSize; break; }
                String header(intern.getRecvBuffer().getBuffer() + currentPos, endLinePos - currentPos);
                if (!parseHeader(intern, header)) return BadRequest;
                currentPos = endLinePos + lineSepSize;
            }

            if(context.query.containsKey("Connection"))
            {
                String * connectionValue = context.query.getValue("Connection");
                if(connectionValue && *connectionValue == "close")
                    intern.closeConnectionOnReply(true);
            }

            // Check if we should purge recv buffer
            intern.getRecvBuffer().Extract(0, currentPos);
            // Hack to stop the server
            if (value && *value == "stop") { running = false; return BadRequest; }
            return Success;
        }

        // Helper method
        static Stream::InputStream * readFromSocketStream(Stream::InputStream * inputStream, const Time::TimeOut & timeout, uint64 amount)
        {
            String content; Stream::OutputStringStream outStr(content);

            // Set the timeout on the socket stream
            Stream::SuccessiveStream * inStream = dynamic_cast<Stream::SuccessiveStream *>(inputStream);
            if (inStream)
            {
                 Stream::SocketInputStream * sockStream = dynamic_cast<Stream::SocketInputStream *>(&inStream->getSecondStream());
                 if (sockStream) sockStream->setTimeout(timeout);
            }

            // Not using copyStream here to support timeout for the input stream
            // Copy the already fetched data at first
            if (!copyStream(*inputStream, outStr, min(inputStream->fullSize(), amount)))
                return new Stream::InputStringStream(content);

            amount -= min(inputStream->fullSize(), amount);

            // Then try to copy as much as possible
            uint8 buffer[512];
            uint64 len = ArrSz(buffer), done = 0;
            while (len == ArrSz(buffer) && done < amount)
            {
                len = inputStream->read(buffer, min((uint64)ArrSz(buffer), amount - done));
                timeout.success();
                if (timeout <= 0) break;
                if (outStr.write(buffer, len) != len) break;
                done += len;
            }

            return new Stream::InputStringStream(content);
        }

        // Get the complete request out of the given input stream and headers.
        Stream::InputStream * EventHTTP::Callback::getCompleteRequest(HeaderMap & headers, Stream::InputStream * inputStream, const Time::TimeOut & timeout)
        {
            // Check the method, if it's a "GET", then we don't care about any data left.
            String * method = headers.getValue("##METHOD##");
            if (method && *method == "GET") return 0; //  Get request don't have a body

            // If the client is using a Transfer-Encoding, we just return nothing as it's not supported right now
            String * transferEncoding = headers.getValue("Transfer-Encoding");
            if (transferEncoding) return 0; // Not supported for now

            // Try to figure out if we are using boundaries
            String * contentType = headers.getValue("Content-Type");

            // If there is no Transfer-Encoding, and no multipart encoding, then the Content-Length header is required by standard.
            // So get the content length
            String * contentLength = headers.getValue("Content-Length");
            if (!contentLength || (contentType && contentType->caselessFind("multipart") != -1))
                // Not standard, so we simply return what we have now
                return readFromSocketStream(inputStream, timeout, (uint64)-1);
            if (contentLength)
            {
                int contentLen = (int)*contentLength;
                return readFromSocketStream(inputStream, timeout, (uint64)contentLen);
            }
/*
            String boundary = contentType->fromFirst("boundary").fromTo("\"", "\"");
            if (boundary.Find("\r")) boundary = boundary.upToFirst("\r");
            if (boundary) boundary = "--" + boundary;
            else boundary = "--myboundary";
            // Then read up to the boundary
*/
            return 0;
        }

        Stream::InputStream * EventHTTP::Callback::makeReply(const String & error, const bool logError, Protocol::HTTP::StatusCode * statusCode, Stream::InputStream * stream)
        {
             if (stream)
             {
                 if (statusCode) *statusCode = Protocol::HTTP::Ok;
                 if (logError) Logger::log(Logger::Network | Logger::Connection, error);
                 return stream;
             } else
             {
                 if (statusCode) *statusCode = Protocol::HTTP::NotFound;
                 if (logError) Logger::log(Logger::Network | Logger::Error, error);
                 return new Stream::InputStringStream(error);
             }
        }

        // Handle the request itself.
        bool EventHTTP::handleRequest(InternalObject & intern, const BaseSocket & client)
        {
            // Check if the resource is available
            Context & context = *(Context *)intern.getPrivateField();
            String * connectionValue = context.query.getValue("Connection");
            if(connectionValue && *connectionValue == "close")
            {
                intern.closeConnectionOnReply(true);
                intern.closeAfterSending();
            } else intern.resetAfterSending();
            intern.corkConnection();
            String * value = context.query.getValue("##RESOURCE##");
            String * method = context.query.getValue("##METHOD##");
            if (!value || !method)
            {
                if (!createAnswerHeader(context, Protocol::HTTP::NotFound)) return false;
                Logger::log(Logger::Error | Logger::Connection, "Resource not found [ERROR]");
                return mergeAnswerHeaders(intern);
            }
            // Ok, prepare the callback
            Utils::ScopePtr<BaseAddress> remoteAddr = client.getPeerName();
            // Check for client closing the connection
            if (!remoteAddr) return false;
            Stream::MemoryBlockStream remainingStream(intern.getRecvBuffer().getBuffer(), intern.getRecvBuffer().getSize());
            Stream::SocketInputStream dataStream(client);
            Stream::SuccessiveStream inStream(remainingStream, dataStream);
            Protocol::HTTP::StatusCode code = Protocol::HTTP::Ok;
            Stream::InputStream * input = callback.clientRequestedEx(*method, *value, context.query, &inStream, *remoteAddr, context, code);
            if (code == Protocol::HTTP::CapturedSocket)
            {   // Ok, the client wanted to capture the socket for its own usage
                intern.captureSocket();
                return true;
            }
            if (!intern.setStreamToSend(input))
                return false;

            // Check for any data to send
            int64 streamSize = intern.getStreamToSend() && intern.getStreamToSend()->fullSize() > 0 ? (int64)intern.getStreamToSend()->fullSize() : 0;
            addAnswerHeader(context, "Content-Length", String::Print(PF_LLD, streamSize));


            if (!createAnswerHeader(context, code)) return false;
            Logger::log(Logger::Connection, "[%s] %s %s: %d", (const char*)remoteAddr->asText(), (const char*)*method, (const char*)*value, (int)code);
            return mergeAnswerHeaders(intern);
        }

        // Handle the request itself.
        bool HTTP::handleRequest(InternalObject & intern, const BaseSocket & client)
        {
            // Check if the resource is available
            Context & context = *(Context *)intern.getPrivateField();
            String * connectionValue = context.query.getValue("Connection");
            if(connectionValue && *connectionValue == "close")
            {
                intern.closeConnectionOnReply(true);
                intern.closeAfterSending();
            } else intern.resetAfterSending();
            intern.corkConnection();
            String * value = context.query.getValue("##RESOURCE##");
            if (!value)
            {
                if (!createAnswerHeader(context, Protocol::HTTP::NotFound)) return false;
                Logger::log(Logger::Error | Logger::Connection, "Resource not found [ERROR]");
                return mergeAnswerHeaders(intern);
            }

#if !defined(NO_FILES_ON_PLATFORM)
            // Then check if the resource access is allowed
            File::Info info(getResource(*value));
            if (!info.checkPermission(File::Info::Reading))
            {
                if (!createAnswerHeader(context, Protocol::HTTP::Forbidden)) return false;
                Logger::log(Logger::Error | Logger::Connection, "Resource not accessible %s [ERROR]", (const char*)info.getFullPath());
                return mergeAnswerHeaders(intern);
            }

            // Ok, it's accessible, so let's build a real answer now
            if (!createAnswerHeader(context, Protocol::HTTP::Ok)) return false;
            String length = String::Print(PF_LLD, info.size);
            addAnswerHeader(context, "Content-Length", length);
            intern.setFileToSend(info);
            Logger::log(Logger::Connection, "Sending %s bytes of %s", (const char*)length, (const char*)info.getFullPath());

            return mergeAnswerHeaders(intern);
#else
            if (!createAnswerHeader(context, Protocol::HTTP::Forbidden)) return false;
            Logger::log(Logger::Error | Logger::Connection, "Resource not accessible %s [ERROR]", (const char *)getResource(*value));
            return mergeAnswerHeaders(intern);
#endif
        }

        // Check if the method is allowed
        bool HTTP::isMethodSupported(const String & method) const
        {
            if (method.caselessCmp("GET") == 0) return true;
            if (method.caselessCmp("POST") == 0) return true;
//            Add other supported method here
//            if (method.caselessCmp("GET") == 0) return true;
            return false;
        }

        // Create the main response header
        bool HTTP::createAnswerHeader(Context & context, const Protocol::HTTP::StatusCode code)
        {
            String output;
            output.Format("HTTP/1.1 %d %s", (int)code, Protocol::HTTP::getMessageForStatusCode(code));

            if (!addAnswerHeader(context, "", output)) return false;
            // Add a default Content-Length if none provided on error
            if (code != Protocol::HTTP::Ok && context.answer.getValue("Content-Length") == 0)
                return addAnswerHeader(context, "Content-Length", "0");
            return true;
        }
    }
}
