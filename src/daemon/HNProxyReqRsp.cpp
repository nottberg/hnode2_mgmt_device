#include <iostream>
#include <sstream>

#include "HNProxyReqRsp.h"

HNProxyHTTPMsg::HNProxyHTTPMsg()
{
    m_headerComplete = false;
    m_dispatched = false;

    m_cSource = NULL;
    m_cSink = NULL;
}

HNProxyHTTPMsg::~HNProxyHTTPMsg()
{

}

void 
HNProxyHTTPMsg::clearHeaders()
{
    m_statusCode = 0;
    m_reason.clear();

    m_contentMoved  = 0;

    m_paramMap.clear();
}

void
HNProxyHTTPMsg::addSCGIRequestHeader( std::string name, std::string value )
{
    // Special handling for headers orginating from the SCGI layer
    // The original CGI interface passed headers via ENV variables 
    // which meant some of the common headers are passed with all
    // capital underscore instead of dash versions of the names.
    // Here segregate that style of header into a seperate list and
    // translate some of them into the more common format.
    if( name.find('_') != std::string::npos )
    {
        // Header contains an underscore so add it to CGI variable map.
        printf( "addCGIVarPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
        m_cgiVarMap.insert( std::pair<std::string, std::string>(name, value) );

        // Do some special handling for some of the CGI values.
        if( Poco::icompare( name, "CONTENT_LENGTH" ) == 0 )
        {
            addHdrPair( "Content-Length", value );
            return;
        }
        else if( Poco::icompare( name, "CONTENT_TYPE") == 0 )
        {
            addHdrPair( "Content-Type", value );
            return;
        }
        else if( Poco::icompare( name, "REQUEST_URI") == 0 )
        {
            setURI( value );
            return;
        }
        else if( Poco::icompare( name, "REQUEST_METHOD") == 0 )
        {
            setMethod( value );
            return;
        }

        // Done processing this header
        return;
    }

    // Not a CGI parameter so add as a normal header
    addHdrPair( name, value );
}

void
HNProxyHTTPMsg::addHdrPair( std::string name, std::string value )
{
    printf( "addHdrPair - name: %s,  value: %s\n", name.c_str(), value.c_str() );
    
    // Handle content length specially so that we always get constant capilization. 
    if( Poco::icompare( name, "Content-Length" ) == 0 )
    {
        m_paramMap.insert(std::pair< std::string, std::string >( "Content-Length", value ) );
        return;
    }

    m_paramMap.insert( std::pair<std::string, std::string>(name, value) );
}

bool 
HNProxyHTTPMsg::hasHeader( std::string name )
{
    std::map< std::string, std::string >::iterator it = m_paramMap.find( name ); 

    if( it == m_paramMap.end() )
        return false;

    return true;
}

const std::string& 
HNProxyHTTPMsg::getURI() const
{
    return m_uri;
}

const std::string& 
HNProxyHTTPMsg::getMethod() const
{
    return m_method;
}

void 
HNProxyHTTPMsg::setDispatched( bool value )
{
    m_dispatched = value;
}

void 
HNProxyHTTPMsg::setHeaderDone( bool value )
{
    m_headerComplete = value;
}

bool 
HNProxyHTTPMsg::isHeaderDone()
{
    return m_headerComplete;
}

bool 
HNProxyHTTPMsg::isDispatched()
{
    return m_dispatched;
}

void 
HNProxyHTTPMsg::setURI( std::string uri )
{
    m_uri = uri;
}

void 
HNProxyHTTPMsg::setMethod( std::string method )
{
    m_method = method;
}

void 
HNProxyHTTPMsg::setStatusCode( uint statusCode )
{
    m_statusCode = statusCode;
}

void 
HNProxyHTTPMsg::setReason( std::string reason )
{
    m_reason = reason;
}

void 
HNProxyHTTPMsg::setContentLength( uint length )
{
    char tmpBuf[64];
    sprintf(tmpBuf, "%u", length);
    m_paramMap.insert( std::pair<std::string, std::string>("Content-Length", tmpBuf) );
}

void 
HNProxyHTTPMsg::setContentType( std::string typeStr )
{
    m_paramMap.insert( std::pair<std::string, std::string>("Content-Type", typeStr) );
}

void 
HNProxyHTTPMsg::configAsNotImplemented()
{
    clearHeaders();

    setStatusCode( 501 );
    setReason("Not Implemented");
    setContentLength( 0 );
}

void 
HNProxyHTTPMsg::configAsNotFound()
{
    clearHeaders();

    setStatusCode( 404 );
    setReason("Not Found");
    setContentLength( 0 );
}

void 
HNProxyHTTPMsg::configAsInternalServerError()
{
    clearHeaders();

    setStatusCode( 500 );
    setReason("Internal Server Error");
    setContentLength( 0 );
}

uint 
HNProxyHTTPMsg::getStatusCode()
{
    return m_statusCode;
}

std::string 
HNProxyHTTPMsg::getReason()
{
    return m_reason;
}

uint 
HNProxyHTTPMsg::getContentLength()
{
    std::map< std::string, std::string >::iterator it = m_paramMap.find( "Content-Length" ); 

    if( it == m_paramMap.end() )
        return 0;

    return strtol( it->second.c_str(), NULL, 0);
}

HNPRR_RESULT_T 
HNProxyHTTPMsg::sendSCGIResponseHeaders()
{
    std::cout << "SCGIResponseHeaders - 1" << std::endl;

    // Get the output stream reference
    if( m_cSink == NULL )
        return HNPRR_RESULT_FAILURE;

    std::cout << "SCGIResponseHeaders - 2" << std::endl;

    std::ostream *outStream = m_cSink->getSinkStreamRef();

    // Add the Status header
    *outStream << "Status: " << getStatusCode() << " " << getReason() << "\r\n";

    // Output other headers
    for( std::map< std::string, std::string >::iterator hit = m_paramMap.begin(); hit != m_paramMap.end(); hit++ )
    {
        *outStream << hit->first << ": " << hit->second << "\r\n";
    }
 
    // Add a blank line to mark the end of the headers
    *outStream << "\r\n";

    // Push this first bit to the server.
    outStream->flush();

    // Move to content send    
    return HNPRR_RESULT_MSG_CONTENT;
}

void 
HNProxyHTTPMsg::setContentSource( HNPRRContentSource *source )
{
    m_cSource = source;
}

void 
HNProxyHTTPMsg::setContentSink( HNPRRContentSink *sink )
{
    m_cSink = sink;
}

std::ostream&
HNProxyHTTPMsg::useLocalContentSource()
{
    m_localContent.clear();
    setContentSource( this );
    return m_localContent; 
}

void
HNProxyHTTPMsg::finalizeLocalContent()
{
    uint size = m_localContent.tellp();
    std::cout << "finalizeLocalContent: " << size << std::endl;
    setContentLength(size);
}

HNPRR_RESULT_T 
HNProxyHTTPMsg::xferContentChunk( uint maxChunkLength )
{
    char buff[4096];

    if( (m_cSource == NULL) || (m_cSink == NULL) )
        return HNPRR_RESULT_FAILURE;

    std::istream *is = m_cSource->getSourceStreamRef();
    std::ostream *os = m_cSink->getSinkStreamRef();

    if( hasHeader( "Content-Length" ) == true )
    {
        uint contentLength = getContentLength();

        if( m_contentMoved >= contentLength )
            return HNPRR_RESULT_MSG_COMPLETE;

        uint bytesToMove = contentLength - m_contentMoved;
        if( bytesToMove > sizeof(buff) )
            bytesToMove = sizeof(buff);

        is->read( buff, bytesToMove );
        std::streamsize bytesRead = is->gcount();
        os->write( buff, bytesRead );
 
        std::cout << "xferContentChunk - bytesMoved: " << bytesRead << std::endl;

        m_contentMoved += bytesRead;

        if( (m_contentMoved < contentLength) && (is->eof() == false) )
            return HNPRR_RESULT_MSG_CONTENT;
    }
    else
    {
        uint bytesToMove = sizeof(buff);

        is->read( buff, bytesToMove );
        std::streamsize bytesRead = is->gcount();
        os->write( buff, bytesRead );
 
        std::cout << "xferContentChunk - bytesMoved: " << bytesRead << std::endl;

        if( is->eof() == false )
            return HNPRR_RESULT_MSG_CONTENT;
    }
    
    // Push everything to server before closing out.
    os->flush();

    return HNPRR_RESULT_MSG_COMPLETE;
}

std::istream* 
HNProxyHTTPMsg::getSourceStreamRef()
{
    return &m_localContent;
}

std::ostream* 
HNProxyHTTPMsg::getSinkStreamRef()
{
    return &m_localContent;
}

void 
HNProxyHTTPMsg::debugPrint()
{
    std::cout << "==== Proxy Request ====" << std::endl;
    std::cout << "== Header Parameter List ==" << std::endl;

    for( std::map< std::string, std::string >::iterator mip = m_paramMap.begin(); mip != m_paramMap.end(); mip++ )
    {
        std::cout << mip->first << " :    " << mip->second << std::endl;
    }
}


HNProxyHTTPReqRsp::HNProxyHTTPReqRsp( uint parentTag )
{
    m_parentTag = parentTag;
}

HNProxyHTTPReqRsp::~HNProxyHTTPReqRsp()
{

}

HNProxyHTTPMsg &
HNProxyHTTPReqRsp::getRequest()
{
    return m_request;
}

HNProxyHTTPMsg &
HNProxyHTTPReqRsp::getResponse()
{
    return m_response;
}

uint 
HNProxyHTTPReqRsp::getParentTag()
{
    return m_parentTag;
}

