/* SPDX-License-Identifier: (GPL-3.0-or-later) */
/* Copyright (C) 2021 X-Ryl669  */


#pragma once

#define UnescapeJSON

#include "Strings/Strings.hpp"

#ifndef IndexType
  #define IndexType int16
#endif

#pragma pack(push, 1)
/** A SAX like JSON on-the-fly parser with no memory allocation while parsing.
    It's inspired by JSMM for working principle but is completely rewritten for C++
    and support partial parsing.
 
    It was developped to fit a memory & flash constrained microcontroller, so many design
    choice are oriented toward saving binary size and memory.
 
    It does not allocate any memory. It's deterministic.
    It's designed for stream parsing (that is, there is no limit to parse end of "logical end")
    It checks the JSON syntax, but accept few invalid constructions:
    - "{}{}" : it'll stop on the first object declaration but not return an error since it hasn't seen the other object.
    - "[123.E232-23++34.24...2424]": Since number are not converted, this will be accepted as an array containing a number.
 
    By default, it does not modify its input and work-in-place.
    It does not convert/unescape strings unless you define UnescapeJSON (in that case, the input text
    is modified to store 0 at strings' ends)
    Even in that case, it does not convert unicode escaped chars (\uABCD) to UTF-8 (usually, this
    is not required since such strings can be returned untouched).
    It does not convert textual number to native number (you can use atoi/atof like functions yourself).
    It tags all objects and arrays with a unique identifier (useful for partial parsing, see below).
    Such identifier is limited to 4095 possible values (before wrap around).
 
    Except for the limitations above, it should be very fast to parse and with minimal code size.
    (On my machine complete code compiles to less than 2.5kB)

    It also supports (optional) partial parsing.
    This allows calling the parse method with an incomplete buffer, start working with the
    partial JSON object, then, when more data is ready, continue parsing from where it lefts off.
    This feature works mostly in zero-copy mode (it works on the input buffer, with no allocation).
    Typically, that's good if you're receiving data from a socket and don't have enough buffers/memory to
    accumulate the complete JSON text before parsing.
 
    Partial parsing is a feature that's almost as big as the parse method in binary so if you don't need it
    you either define SkipJSONPartialParsing, or build with "-ffunction-sections -fdata-sections -Wl,-gc-sections"
    flags so the linker will garbage collect the function.
    This removes 800 bytes from the compiled binary on my machine.
    The function is logically written so it resumes after the initial parse function returned Starving error.
 
    A partial parsing rewrites the token stream so the object hierarchy to the last values (with key if available) found is kept valid
    but all previous values (opt. keys) are removed. For ease of use reasons, if the parsing stopped in the middle of a a key/value pair,
    the token stream will contains the last key the (interrupted and resumed) value refers to.
    When processing the token stream, you are ensured to always have a key before a value in an object.
    Since object and arrays are identified, you can also figure out in which object the key/value refers to by
    remembering the container's ID. Try to accumulate as much data as possible before calling partialParsing,
    since this rewriting is computation expensive, so avoid to do that for each byte received.
 
    The default implementation limits input JSON size to signed 16 bits (32768 bytes), and less than 4095 embedded
    objects/arrays.
    If you intend to parse more than that, you'll need to define IndexType to a larger **signed** type.
    The Token's size is, by default, 8 bytes long. Using signed 32 bits int for the IndexType will double it's size.
    The parser memory size is 10 bytes by default. It requires less than a hundred bytes of stack space.
 
    Because I got the question, you can not write a JSON stream with any parser and even more with this one.
    Writing small JSON is trivial, but it's not the subject of this class.
 
    There is no dependency on STL, and no exceptions either.
    Only memmove is used in partialParsing for ensuring the previous key is present in the new stream to parse. */
struct JSON
{
    /** The invalid position */
    enum { InvalidPos = (IndexType)-1 };

    /** The possible parsing result */
    enum ParsingResult
    {
        NotEnoughTokens     = -1,   //!< Not enough tokens provided
        Invalid             = -2,   //!< Invalid format for the given input
        Starving            = -3,   //!< Not enough data, need to call with more data
        NeedRefill          = -4,   //!< Only when using partialParse, this error tells you to refill the text buffer
    };

    /** One of the token that parse & parsePartial function is filling.
        The token stores the type of the element found, its position in the stream, and its relation to the parent container.
        When referring containers, the end position of the container in the stream is not saved, instead the number of child elements is stored
        and an (unique) identifier. */
    struct Token
    {
        /** And it's type */
        enum Type
        {
            Undefined    = 0,
            Object       = 1,
            Array        = 2,
            Key          = 3,
            String       = 4,
            Null         = 5,
            True         = 6,
            False        = 7,
            Number       = 8,
        };
        /** The token id (only valid for container, like objects and array) */
        unsigned short   id   : 12;
        /** The token type */
        unsigned short   type : 4;
        /** The token parent index */
        IndexType   parent;
        /** The token start position */
        IndexType   start;
        union
        {
            /** The end position (for primitive values) */
            IndexType end;
            /** The number of elements (for objects and array) */
            IndexType elementCount;
        };
        void init(Type type, IndexType parent, IndexType start, IndexType end, IndexType id = 0) {  this->id = id; this->type = type; this->parent = parent; this->start = start; this->end = end; }
        IndexType changeType(Type type) { this->type = type; return 0; }
#ifdef UnescapeJSON
        /** Unescape the string (or key).
            This modifies the given input buffer to make it a valid zero terminated string */
        Strings::FastString unescape(char * in);
#endif
    };

    /** The current position */
    IndexType pos;
    /** The next token in the given array */
    IndexType next;
    /** The super/parent token index */
    IndexType super;
    /** The last used id */
    unsigned short lastId;
    /** The current parsing state */
    enum State
    {
        ExpectValue = 0,
        ExpectKey   = 1,
        ExpectColon = 2,
        ExpectComma = 3,

        Done        = 4,
    };

    enum PartialState
    {
        NotUsed     = 0,
        NeedFixing,
    };
    /** The current state */
    char    state;
    /** The partial state, if used */
    char    partialState;

    /** Construction */
    JSON() : pos(0), next(0), super(InvalidPos), lastId(0), state(0), partialState(0) {}

    /** Set new source data to parse.
        If the function starved, you should either feed more data via in, or use the famine method below */
    IndexType parse(const char * in, const IndexType len, Token * tokens, const IndexType tokenCount);
#ifndef SkipJSONPartialParsing
    /** Get the number of used (and valid) tokens so far.
        The idea is that you have not received as many data as required to finish parsing, yet,
        it's the same buffer that'll receive the next chunk of data, so you can't realloc it to store the new data.
        So, you'll extract as much complete tokens as possible with this method.
        The "complete" tokens are key value pair (you can't have only a key) and finished array items found so far.
        Unfinished container tokens up to the last done are incomplete you'll find that their elementCount is 0.

        The incomplete tokens (typically the current value/key and all parent containers) will need to be fixed.
        This method will fix all such tokens by setting InvalidPos in start (for containers) and moving them in
        front of the token array.
        Then it'll memmove the last string position for the last valid token to the beginning of the given input buffer,
        and adjust the remaining length. You can then resume receiving starting from that marker.

        @warning You must use the key & value content before calling this again because they will be erased.

        @param in           The text buffer to parse.
        @param len          On input, contains the size of the text buffer in bytes. On output, will be set to the first position to overwrite safely.
        @param tokens       The tokens buffer to store into
        @param tokenCount   The number of tokens in the buffer
        @param lastTokenPos On output, will contain the position to continue reading tokens from (all tokens before this position are container you'll already received)
        @return 0 when it's done parsing the JSON stream (please notice that you'll get all the tokens in a previous call), a negative
                value on error, the number of tokens used in the tokens buffer upon success

        Typical usage is:
        @code
            JSON j;
            IndexType ret = j.parse(buf, len, tokens, count);
            if (ret > 0) useTokens(&tokens[0] ... &tokens[ret]);
            else if (ret < 0 && ret > Starving) return errorWithJSON(ret);
            else while (ret)
            {
                IndexType firstNew = 0;
                ret = j.parsePartial(buf, len, tokens, count, firstNew);
                if (ret == NeedRefill)
                {
                    refillBuffer(&buf[len], initialBufferLen - len);
                    continue;
                }
                if (ret >= 0)
                    useTokens(&tokens[firstNew] ... &tokens[ret]);
                else if (ret < 0 && ret > Starving) return errorWithJSON(ret);
            }
        @endcode
        */
    IndexType partialParse(char * in, IndexType & len, Token * tokens, const IndexType tokenCount, IndexType & lastTokenPos);
#endif

private:
    /** Allocate a token */
    IndexType allocToken(const IndexType tokenCount) { if (next >= tokenCount) return InvalidPos; return next++; }
    /** Match the given input string with the expected pattern */
    IndexType match(const char * in, const IndexType len, Token & token, const char * pattern);
    /** Parse the given primitive */
    IndexType parsePrimitive(const char * in, const IndexType len, Token & token);
    /** Parse the given string */
    IndexType parseString(const char * in, const IndexType len, Token & token);
    /** Filter return */
    IndexType rememberLastError(IndexType in) { if (in < 0) partialState = (char)in; else partialState = 0; return in; }
};
#pragma pack(pop)


