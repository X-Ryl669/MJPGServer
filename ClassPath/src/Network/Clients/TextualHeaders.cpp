// We need our declaration
#include "../../../include/Network/Clients/TextualHeaders.hpp"
#include "../../../include/Crypto/Random.hpp"


namespace Network
{
   namespace Client
   {
        // Add an header to the buffer.
        bool TextualHeaders::addHeader(const String & header, const String & value, const bool replaceExisting)
        {
            Strings::StringArray * existing = headers.getValue(header);
            if (existing)
            {
                if (replaceExisting) existing->getElementAtUncheckedPosition(0) = value;
                else existing->Append(value);
            }
            else
            {
                existing = new Strings::StringArray;
                if (!existing) return false;
                existing->Append(value);
                if (!headers.storeValue(header, existing, true)) return false;
            }
            return true;
        }
       
        bool TextualHeaders::parseHeader(const String & header, const bool replaceExisting)
        {
            int pos = header.Find(':');
            if (pos != -1)
            {
                // Get the header name
                String headerName = fixHeaderName(header.midString(0, pos));
                
                // And the value
                String headerValue = header.midString(pos+1, header.getLength());
                headerValue.Trim();
                return addHeader(headerName, headerValue, replaceExisting);
            }
            return false;
        }

        // Fix header name so it fits the Xxxxx-Xxxxx case
        String TextualHeaders::fixHeaderName(String headerName)
        {
            headerName.Trim();
            // Convert name to "HTTP standard" defined name
            headerName.toLowercase();
            int letterPos = 0;
            while (letterPos != -1)
            {
                headerName[letterPos] = (char)(headerName[letterPos] - (char)32);
                letterPos = headerName.Find('-', letterPos);
                if (letterPos != -1) letterPos++;
            }
            return headerName;
        }

        // Find header value in header buffer
        const String TextualHeaders::findLastHeaderValue(const String & _headerName, const bool formatHeaderName) const
        {
            String headerName = formatHeaderName ? fixHeaderName(_headerName) : _headerName;
            Strings::StringArray * array = headers.getValue(headerName);
            if (!array || !array->getSize()) return String();
            return (*array)[array->getSize() - 1];
        }

        // Find header value in header buffer
        const Strings::StringArray * TextualHeaders::findHeaderValueArray(const String & _headerName, const bool formatHeaderName) const
        {
            String headerName = formatHeaderName ? fixHeaderName(_headerName) : _headerName;
            Strings::StringArray * array = headers.getValue(headerName);
            return array;
        }

        // Helper functor to merge all headers
        struct Merger
        {
            String & output;
            int mergeHeader(const String & key, const Strings::StringArray * t)
            {
                String keyStart = key ? key + ": " : "";
                if (t)
                {
                    for (size_t i = 0; i < t->getSize(); i++)
                    {
                        output += keyStart;
                        output += t->getElementAtPosition(i);
                        output += "\r\n";
                    }
                }
                else output += keyStart + "\r\n";
                return 1;
            }
            Merger(String & output) : output(output) {}
        };

        // Merge all headers to data
        bool TextualHeaders::mergeHeaders(String & output)
        {
            Merger merge(output);
            headers.iterateAllEntries(merge, &Merger::mergeHeader);
            if (output.getLength() == 0) return false;
            output += "\r\n";
            return true;
        }

        // Generate a MIME compliant boundary
        void TextualHeaders::generateBoundary(String & boundary)
        {
            boundary = "";
            for (int i = 0; i < 16; i++) boundary += "azertyuiopqsdfghjklmwxcvbn1234056789"[Random::numberBetween(0, 36)];
        }
   }
}
