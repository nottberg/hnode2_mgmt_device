#ifndef __HN_PROXY_REQRSP_H__
#define __HN_PROXY_REQRSP_H__

#include "Poco/Net/HTTPRequest.h"

#include <string>
#include <map>

namespace pn = Poco::Net;

class HNProxyRequest : public pn::HTTPRequest
{
    private:
        std::string m_uri;
        std::string m_method;
        uint32_t m_contentLength;
        
        bool m_headerComplete;
        bool m_dispatched;
        
        // 
        std::map< std::string, std::string > m_paramMap;
        
    public:
        HNProxyRequest();
       ~HNProxyRequest();
        
        void setHeaderDone( bool value );
        bool isHeaderDone();
        
        void setDispatched( bool value );
        bool isDispatched();     
       
        void addHdrPair( std::string name, std::string value );

        const std::string& getURI() const;
        const std::string& getMethod() const;
};

class HNProxyResponse
{
    private:
    
    public:
        HNProxyResponse();
       ~HNProxyResponse();

};

#endif //__HN_PROXY_REQRSP_H__
