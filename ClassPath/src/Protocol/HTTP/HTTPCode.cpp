// We need our declaration
#include "../../../include/Protocol/HTTP/HTTPCode.hpp"

namespace Protocol
{
    namespace HTTP
    {
        // Few of below are required. Need to modify
        const char * getMessageForStatusCode(const StatusCode code)
        {
            switch (code)
            {
                // ----------------------SUCCESS CODES--------------------//
            case Ok:
                return "OK";
            case Created:
                return "Created";
            case Accepted:
                return "Accepted";
            case NonAuthInfo:
                return "Non-Authoritative Information";
            case NoContent:
                return "No Content";
            case ResetContent:
                return "Reset Content";
            case PartialContent:
                return "Partial Content";
                // ---------------------RE-DIRECTION CODES-----------------//
            case MultipleChoices:
                return "Multiple Choices";
            case MovedForEver:
                return "Moved Permanently";
            case MovedTemporarily:
                return "Moved Temporarily";
            case SeeOther:
                return "See Other";
            case NotModified:
                return "Not Modified";
            case UseProxy:
                return "Use Proxy";
            case Unused:
                return "Unused";
            case TemporaryRedirect:
                return "Temporary Redirect";
                // ---------------CLIENT ERROR CODES-------------------//
            case BadRequest:
                return "Bad Request";
            case Unauthorized:
                return "Unauthorized";
            case PaymentRequired:
                return "Payment Required";
            case Forbidden:
                return "Forbidden";
            case NotFound:
                return "Not Found";
            case BadMethod:
                return "Method Not Allowed";
            case NotAcceptable:
                return "Not Acceptable";
            case ProxyRequired:
                return "Proxy Authentication Required";
            case TimedOut:
                return "Request Time-out";
            case Conflict:
                return "Conflict";
            case Gone:
                return "Gone";
            case LengthRequired:
                return "Length Required";
            case PreconditionFail:
                return "Precondition Failed";
            case EntityTooLarge:
                return "Request Entity Too Large";
            case URITooLarge:
                return "Request-URI Too Large";
            case UnsupportedMIME:
                return "Unsupported Media Type";
            case RequestRange:
                return "Requested Range Not Satisfiable";
            case ExpectationFail:
                return "Expectation Failed";
                // --------------------SERVER ERROR CODES-----------------//
            case InternalServerError:
                return "Internal Server Error";
            case NotImplemented:
                return "Not Implemented";
            case BadGateway:
                return "Bad Gateway";
            case Unavailable:
                return "Service Unavailable";
            case GatewayTimedOut:
                return "Gateway Time-out";
            case UnsupportedHTTPVersion:
                return "HTTP Version not supported";
            case ConnectionTimedOut:
                return "The connection to the server timed out";
                // --------------------SERVER ERROR CODES-----------------//
            case CapturedSocket:
                return "Internal client socket captured";
                // ---------------------DEFAULT--------------------------//
            default:
                return "Unknown error or state of connection";
            }
        }
    }
}
