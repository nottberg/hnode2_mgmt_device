#ifndef _HN_MANAGED_DEVICE_ARBITER_H_
#define _HN_MANAGED_DEVICE_ARBITER_H_

#include <string>
#include <map>
#include <mutex>

#include "hnode2/HNodeID.h"

// Forward declaration for friend class below
class HNMDARunner;

typedef enum HNManagedDeviceListResultEnum
{
    HNMDL_RESULT_SUCCESS,
    HNMDL_RESULT_FAILURE
}HNMDL_RESULT_T;

typedef enum HNManagedDeviceCommandExecutionStateEnum
{
    HNMDC_EXEC_STATE_IDLE,
    HNMDC_EXEC_STATE_READY,
    HNMDC_EXEC_STATE_EXECUTING,
    HNMDC_EXEC_STATE_DONE
}HNMDC_EXEC_STATE_T;

typedef enum HNManagedDeviceCommandTypeEnum
{
    HNMDC_CMDTYPE_NOTSET,           // Set device parameters
    HNMDC_CMDTYPE_CLAIMDEV,         // Claim ownership of an available device
    HNMDC_CMDTYPE_RELEASEDEV,       // Release ownership of an owned device
    HNMDC_CMDTYPE_SETDEVPARAMS      // Set configurable device parameters
}HNMDC_CMDTYPE_T;

typedef enum HNManagedDeviceCommandParameterMaskEnum
{
    HNMDC_FLDMASK_NONE  = 0x00000000,
    HNMDC_FLDMASK_NAME  = 0x00000001
}HNMDC_FLDMASK_T;

class HNMDMgmtCmd
{
    public:
         HNMDMgmtCmd();
        ~HNMDMgmtCmd();

        void clear();

        bool setTypeFromStr( std::string value );
        void setName( std::string value );

        HNMDL_RESULT_T setFromJSON( std::istream *bodyStream );

        HNMDC_CMDTYPE_T getType();
        std::string getTypeAsStr();

        uint getFieldMask();
    
        std::string getName();

        void setExecState( HNMDC_EXEC_STATE_T value );
        HNMDC_EXEC_STATE_T getExecState();

        void getUpdateFieldsJSON( std::ostream &os );

    private:
        HNMDC_EXEC_STATE_T m_execState;

        HNMDC_CMDTYPE_T m_cmdType;

        uint        m_fieldMask;
        std::string m_name;
};

typedef enum HNMDARAddressTypeEnum
{
    HMDAR_ADDRTYPE_NOTSET,
    HMDAR_ADDRTYPE_UNKNOWN,
    HMDAR_ADDRTYPE_IPV4,
    HNDAR_ADDRTYPE_LOOPBACK_IPV4,
    HNDAR_ADDRTYPE_CAST_IPV4,
    HNDAR_ADDRTYPE_INET_IPV4,
    HMDAR_ADDRTYPE_IPV6,
    HNDAR_ADDRTYPE_LOOPBACK_IPV6,
    HNDAR_ADDRTYPE_CAST_IPV6,
    HNDAR_ADDRTYPE_INET_IPV6 
}HMDAR_ADDRTYPE_T;

class HNMDARAddress
{
    private:
        HMDAR_ADDRTYPE_T   m_type;
        std::string        m_dnsName;
        std::string        m_address;
        uint16_t           m_port;

    public:
        HNMDARAddress();
       ~HNMDARAddress();

        void setAddressInfo( std::string dnsName, std::string address, uint16_t port );

        HMDAR_ADDRTYPE_T  getType();
        std::string getTypeAsStr();

        std::string getDNSName();
        std::string getAddress();
        uint16_t    getPort();

        std::string getURL( std::string protocol, std::string path );

        void debugPrint( uint offset );
};

typedef enum HNManagedDeviceRecordManagementStateEnum
{
    HNMDR_MGMT_STATE_NOTSET,        // Default value
    HNMDR_MGMT_STATE_DISCOVERED,    // Added via Avahi Discovery
    HNMDR_MGMT_STATE_RECOVERED,     // Added from config file, prior association
    HNMDR_MGMT_STATE_SELF,          // This record is for myself - the management device itself
    HNMDR_MGMT_STATE_OPT_INFO,      // REST read to aquire basic operating info
    HNMDR_MGMT_STATE_OWNER_INFO,    // REST read for current ownership
    HNMDR_MGMT_STATE_UNCLAIMED,     // Device is waiting to be claimed 
    HNMDR_MGMT_STATE_NOT_AVAILABLE, // Device is not currently owned, but not available for claiming 
    HNMDR_MGMT_STATE_OTHER_MGR,     // Device is currently owner by other manager
    HNMDR_MGMT_STATE_ACTIVE,        // Device is active, responding to period health checks  
    HNMDR_MGMT_STATE_DISAPPEARING,  // Avahi notification that device is offline
    HNMDR_MGMT_STATE_OFFLINE,       // Recent attempts to contact device have been unsuccessful
    HNMDR_MGMT_STATE_EXEC_CMD       // Execute a command to the device
}HNMDR_MGMT_STATE_T;

typedef enum HNManagedDeviceRecordOwnerStateEnum
{
    HNMDR_OWNER_STATE_NOTSET,
    HNMDR_OWNER_STATE_UNKNOWN,
    HNMDR_OWNER_STATE_MINE,
    HNMDR_OWNER_STATE_OTHER,
    HNMDR_OWNER_STATE_AVAILABLE,
    HNMDR_OWNER_STATE_UNAVAILABLE
}HNMDR_OWNER_STATE_T;

class HNMDARecord
{
    private:
        HNMDR_MGMT_STATE_T   m_mgmtState;
        HNMDR_OWNER_STATE_T  m_ownerState;

        std::string m_discID;
        HNodeID     m_hnodeID;
        std::string m_devType;
        std::string m_devVersion;
        std::string m_instance;
        std::string m_name;

        std::vector< HNMDARAddress > m_addrList;

        HNodeID     m_ownerHNodeID;

        HNMDMgmtCmd m_mgmtCmd;

        // A mutex for guarding record modifications.
        std::mutex *m_deviceMutex;

    public:
        HNMDARecord();
        HNMDARecord( const HNMDARecord &srcObj );
       ~HNMDARecord();

        void lockForUpdate();
        void unlockForUpdate();

        void setManagementState( HNMDR_MGMT_STATE_T value );
        void setOwnershipState( HNMDR_OWNER_STATE_T value );

        void setDiscoveryID( std::string value );
        void setDeviceType( std::string value );
        void setDeviceVersion( std::string value );
        void setHNodeIDFromStr( std::string value );
        void setInstance( std::string value );
        void setName( std::string value );

        void addAddressInfo( std::string dnsName, std::string address, uint16_t port );

        void setOwnerID( HNodeID &ownerID );
        void clearOwnerID();

        HNMDR_MGMT_STATE_T  getManagementState();
        std::string getManagementStateStr();

        HNMDR_OWNER_STATE_T getOwnershipState();
        std::string getOwnershipStateStr();

        std::string getDiscoveryID();
        std::string getDeviceType();
        std::string getDeviceVersion();
        std::string getHNodeIDStr();
        std::string getInstance();
        std::string getName();
        std::string getCRC32ID();

        void getAddressList( std::vector< HNMDARAddress > &addrList );

        HNMDL_RESULT_T findPreferredConnection( HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo );
        
        HNMDL_RESULT_T updateRecord( HNMDARecord &newRecord );

        HNMDL_RESULT_T setMgmtCmdFromJSON( std::istream *bodyStream );

        HNMDMgmtCmd& getDeviceMgmtCmdRef();

        void debugPrint( uint offset );
};

class HNManagedDeviceArbiter
{
    private:
        // The HNodeID for the management node itself.
        HNodeID     m_selfHnodeID;

        // A mutex over the device map modifications
        std::mutex m_mapMutex;

        // A map of known hnode2 devices
        std::map< std::string, HNMDARecord > mdrMap;

        // The thread helper
        void *thelp;

        // Should the monitor still be running.
        bool runMonitor;

        uint m_monitorWaitTime;

        void setNextMonitorState( HNMDARecord &device, HNMDR_MGMT_STATE_T nextState, uint minValue );

        HNMDL_RESULT_T updateDeviceOperationalInfo( HNMDARecord &device );
        HNMDL_RESULT_T updateDeviceOwnerInfo( HNMDARecord &device );
        HNMDL_RESULT_T sendDeviceClaimRequest( HNMDARecord &device );
        HNMDL_RESULT_T sendDeviceReleaseRequest( HNMDARecord &device );
        HNMDL_RESULT_T sendDeviceSetParameters( HNMDARecord &device );

        HNMDL_RESULT_T executeDeviceMgmtCmd( HNMDARecord &device );

    protected:
        void runMonitoringLoop();
        void killMonitoringLoop();

    public:
        HNManagedDeviceArbiter();
       ~HNManagedDeviceArbiter();

        HNMDL_RESULT_T notifyDiscoverAdd( HNMDARecord &record );
        HNMDL_RESULT_T notifyDiscoverRemove( HNMDARecord &record );

        void setSelfInfo( std::string hnodeIDStr );
        std::string getSelfHNodeIDStr();
        std::string getSelfCRC32ID();

        void start();
        void shutdown();

        HNMDL_RESULT_T getDeviceCopy( std::string crc32ID, HNMDARecord &device );
        void getDeviceListCopy( std::vector< HNMDARecord > &deviceList );

        HNMDL_RESULT_T lookupConnectionInfo( std::string crc32ID, HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo );

        HNMDL_RESULT_T setDeviceMgmtCmdFromJSON( std::string crc32ID, std::istream *bodyStream );

        HNMDL_RESULT_T startDeviceMgmtCmd( std::string crc32ID );

        void debugPrint();

    friend HNMDARunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
