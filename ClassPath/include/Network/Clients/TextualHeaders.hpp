#ifndef hpp_CPP_TextualHeaders_CPP_hpp
#define hpp_CPP_TextualHeaders_CPP_hpp

// We need strings too
#include "../../Strings/Strings.hpp"
// We need hash tables
#include "../../Hash/HashTable.hpp"

namespace Network
{
    namespace Client
    {
        /** The String class we are using */
        typedef Strings::FastString String;
        /** This class is used to provide textual headers parsing, storing and generating facility.
            Code duplication is avoided since multiple protocol use a common format, that's handled by this class.
            The header map is a String => StringArray map even if most protocol only require String => String map. */
        struct TextualHeaders
        {
            // Type definition and enumeration
        public:
            /** The header map we are using */
            typedef Container::HashTable<Strings::StringArray, String, Container::HashKey<String>, Container::DeletionWithDelete<Strings::StringArray>, false, true > HeaderMap;

            // Members
        protected:
            /** The headers */
            HeaderMap   headers;

        public:
            /** Add multiple headers at once, there must be one per line, "\n" or "\r\n" separated.
                Headers must be in the standard "Key: Value" format.
                @param headers          The text to parse and add to the array.
                @param replaceExisting  If the header already exist, try to replace it, instead of appending to the array
                @return true on successful adding of all headers. If any failed, return false */
            bool addHeaders(String headers, const bool replaceExisting = false)
            {
                String h = headers.splitUpTo("\n");
                while (parseHeader(h, replaceExisting) && headers) h = headers.splitUpTo("\n");
                return !headers;
            }
            /** Add an header to the buffer.
                @param headerName       The header name
                @param headerValue      The header value
                @param replaceExisting  If the header already exist, try to replace it, instead of appending to the array
                @warning This method must be called before calling emitRequest (or fetchResult) on the network client */
            bool addHeader(const String & headerName, const String & headerValue, const bool replaceExisting = false);
            /** Find header value in header buffer.
                If multiple header were stored, this returns the last one found (valid for most header, but not for all)
                @param headerName       The header name to look for
                @param formatHeaderName If true, the header name to look for is formatted to follow HTTP standard, like "Capitalized-Words"
                @return the value for the specified header or empty string on error */
            const String findLastHeaderValue(const String & headerName, const bool formatHeaderName = false) const;
            /** Find header values in header buffer.
                If multiple header were stored, this returns the array of strings for the values
                @param headerName       The header name to look for
                @param formatHeaderName If true, the header name to look for is formatted to follow HTTP standard, like "Capitalized-Words"
                @return 0 on error, or a pointer to a self maintained StringArray (no need to delete it) */
            const Strings::StringArray * findHeaderValueArray(const String & headerName, const bool formatHeaderName = false) const;
            /** Parse the given header from the given string and add to the map.
                @param headerText       The header value
                @param replaceExisting  If the header already exist, try to replace it, instead of appending to the array */
            bool parseHeader(const String & headerText, const bool replaceExisting = false);
            /** Clear the intermediate headers.
                You'll need to call this yourself if you're going to emit a new request after a previous one. */
            void clearHeaders() { headers.clearTable(); }
            /** Merge the headers.
                @return A string that ends with "\r\n\r\n" */
            bool mergeHeaders(String & output);

            /** Generate a MIME compliant boundary that should never appear in the final stream.
                Not related to this, but useful in multiple subclass, so it's put here.
                The boundary does not start with "--" so you need to add it yourself wherever appropriate.
                This is only probabilistically correct, since we don't actually know the stream */
            static void generateBoundary(String & boundary);
            /** Fix header name so it fits the Xxxxx-Xxxxx case */
            static String fixHeaderName(String headerName);

        protected:
            /** Clone the headers */
            TextualHeaders * cloneHeaders() { TextualHeaders * t = new TextualHeaders; t->headers = headers; return t; }
        };
    }
}

#endif
