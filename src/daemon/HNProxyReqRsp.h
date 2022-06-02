#ifndef __HN_PROXY_REQRSP_H__
#define __HN_PROXY_REQRSP_H__

#include "Poco/Net/HTTPRequest.h"

#include <string>
#include <map>
#include <sstream>

namespace pn = Poco::Net;

typedef enum HNPRRResultEnumeration {
    HNPRR_RESULT_SUCCESS,
    HNPRR_RESULT_MSG_CONTENT,
    HNPRR_RESULT_MSG_COMPLETE,
    HNPRR_RESULT_FAILURE
} HNPRR_RESULT_T;

class HNPRRContentSource
{
    public:
        virtual std::istream* getSourceStreamRef() = 0;
};

class HNPRRContentSink
{
    public:
        virtual std::ostream* getSinkStreamRef() = 0;
};

class HNProxyHTTPMsg : public HNPRRContentSource, public HNPRRContentSink
{
    private:
        std::string m_uri;
        std::string m_method;
        std::string m_contentType;
        std::string m_reason;

        uint m_statusCode;
        uint m_contentLength;
        
        bool m_headerComplete;
        bool m_dispatched;
        
        // 
        std::map< std::string, std::string > m_paramMap;
        
        HNPRRContentSource  *m_cSource;
        HNPRRContentSink    *m_cSink;

        std::stringstream    m_localContent;
        uint                 m_contentMoved;

        void buildExtraHeaders( std::ostream &outStream );

    public:
        HNProxyHTTPMsg();
       ~HNProxyHTTPMsg();
        
        void clearHeaders();

        void setHeaderDone( bool value );
        bool isHeaderDone();
        
        void setDispatched( bool value );
        bool isDispatched();     
       
        void setStatusCode( uint statusCode );
        void setReason( std::string reason );

        void setContentType( std::string typeStr );
        void setContentLength( uint length );

        void configAsNotImplemented();
        void configAsNotFound();
        void configAsInternalServerError();

        uint getStatusCode();
        std::string getReason();

        std::string getContentType();
        uint getContentLength();

        void addHdrPair( std::string name, std::string value );

        HNPRR_RESULT_T sendHeaders();

        const std::string& getURI() const;
        const std::string& getMethod() const;
    
        void setContentSource( HNPRRContentSource *source );
        void setContentSink( HNPRRContentSink *sink );

        std::ostream& useLocalContentSource();
        void finalizeLocalContent();

        HNPRR_RESULT_T xferContentChunk( uint maxChunkLength );

        std::istream* getSourceStreamRef();
        std::ostream* getSinkStreamRef();

        void debugPrint();
};

class HNProxyHTTPReqRsp
{
    private:
        uint m_parentTag;

        HNProxyHTTPMsg    m_request;
        HNProxyHTTPMsg    m_response;

    public:
        HNProxyHTTPReqRsp( uint parentTag );
       ~HNProxyHTTPReqRsp();

        uint getParentTag();

        HNProxyHTTPMsg &getRequest();
        HNProxyHTTPMsg &getResponse();

};

#endif //__HN_PROXY_REQRSP_H__
