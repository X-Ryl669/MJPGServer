#ifndef hpp_CPP_HTTPCode_CPP_hpp
#define hpp_CPP_HTTPCode_CPP_hpp

/** All network protocol specific structure or enumerations are declared here */
namespace Protocol
{
    /** The HTTP specific enumeration or structures (RFC2616) */
    namespace HTTP
    {
        /** The HTTP status codes as defined in http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html */
        enum StatusCode
        {
            Continue        = 100,  //!< This is a bastard answer that requires parsing it again (it's never returned, but ignored)

            Ok              = 200,  //!< The request processing succeeded
            Created         = 201,  //!< The request was created
            Accepted        = 202,  //!< The request was accepted
            NonAuthInfo     = 203,  //!< Non authoritative information provided
            NoContent       = 204,  //!< No content found
            ResetContent    = 205,  //!< The server reset the content
            PartialContent  = 206,  //!< The server sent partial content

            MultipleChoices     = 300,  //!< The server gave redirection choices
            MovedForEver        = 301,  //!< The content moved permanently
            MovedTemporarily    = 302,  //!< The content moved temporarily
            SeeOther            = 303,  //!< Please see the other url
            NotModified         = 304,  //!< The content wasn't modified
            UseProxy            = 305,  //!< The use of a proxy is not allowed
            Unused              = 306,  //!< This content is not used
            TemporaryRedirect   = 307,  //!< There is a temporary redirection in place

            BadRequest      = 400,  //!< The server doesn't understand the request
            Unauthorized    = 401,  //!< The server doesn't grant access to this resource
            PaymentRequired = 402,  //!< Access to this resource requires payment
            Forbidden       = 403,  //!< The server denied access to the content
            NotFound        = 404,  //!< The requested content wasn't found
            BadMethod       = 405,  //!< The used method is not allowed
            NotAcceptable   = 406,  //!< The request is not acceptable
            ProxyRequired   = 407,  //!< Proxy authentication required
            TimedOut        = 408,  //!< The request timed out
            Conflict        = 409,  //!< The server encountered a conflict on the resource
            Gone            = 410,  //!< The content is gone
            LengthRequired  = 411,  //!< The request length is required
            PreconditionFail= 412,  //!< The precondition failed
            EntityTooLarge  = 413,  //!< The request entity is too large
            URITooLarge     = 414,  //!< The request URI is too large
            UnsupportedMIME = 415,  //!< The given media type is not supported
            RequestRange    = 416,  //!< Requested range is not correct
            ExpectationFail = 417,  //!< Expectation failed


            InternalServerError     = 500,  //!< The server present an internal error
            NotImplemented          = 501,  //!< The requested resource or method isn't implemented
            BadGateway              = 502,  //!< The server use a bad gateway
            Unavailable             = 503,  //!< The service is unavailable
            GatewayTimedOut         = 504,  //!< The gateway timed out
            UnsupportedHTTPVersion  = 505,  //!< The given HTTP version is not supported
            ConnectionTimedOut      = 522,  //!< The connection to the server timed out
            
            CapturedSocket          = 600,  //!< This is for handling Upgrade mechanism.
        };

        /** Check is the HTTP answer code is a status code (meaning success) */
        inline bool isStatus(const StatusCode code)         { return (int)code < 300; }
        /** Check is the HTTP answer code is a redirection code (meaning need to contact another server) */
        inline bool isRedirection(const StatusCode code)    { return (int)code >= 300 && (int)code < 400; }
        /** Check is the HTTP answer code is a client error code (meaning server refused the client parameters) */
        inline bool isClientError(const StatusCode code)    { return (int)code >= 400 && (int)code < 500; }
        /** Check is the HTTP answer code is a server error code (meaning server doesn't work) */
        inline bool isServerError(const StatusCode code)    { return (int)code >= 500; }

        /** Get a textual message for a given HTTP status code
            @param code     The HTTP status code to check
            @return A pointer on a static textual message for the status code */
        const char * getMessageForStatusCode(const StatusCode code);

    }
}

#endif
