/************************************************************************
*	DICOMLIB
*	Copyright 2003 Sunnybrook and Women's College Health Science Center
*	Implemented by Trevor Morgan  (morgan@sten.sunnybrook.utoronto.ca)
*
*	See LICENSE.txt for copyright and licensing info.
*************************************************************************/


#ifndef NDIMSE_HPP_INCLUDE_GUARD_5834956123
#define NDIMSE_HPP_INCLUDE_GUARD_5834956123

#include "DataSet.hpp"
#include "UID.hpp"
#include "ServiceBase.hpp"
#include "CommandSets.hpp"
#include <functional>
#include <vector>

/*
	Is inheritance the correct mechanism to use here, or 
	would composition be more appropriate.  I.e, are we dealing
	with 'IS A' or 'HAS A'?
*/


/*
	This needs the same treatment as CDIMSE - replacing virtual functions
	with function objects as our abstraction mechanism of choice.
*/

namespace dicom
{
	bool IsNdimseSuccessStatus(UINT16 status);
	bool IsNdimseWarningStatus(UINT16 status);
	bool IsNdimseFailureStatus(UINT16 status);
	bool IsNdimseFinalStatus(UINT16 status);
	bool IsNEventReportResponseStatus(UINT16 status);
	bool IsNGetResponseStatus(UINT16 status);
	bool IsNSetResponseStatus(UINT16 status);
	bool IsNActionResponseStatus(UINT16 status);
	bool IsNCreateResponseStatus(UINT16 status);
	bool IsNDeleteResponseStatus(UINT16 status);

	typedef std::function<UINT16(ServiceBase&,const DataSet&,const DataSet&,DataSet&)>
		NHandlerFunction;

	void HandleNEventReport(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	void HandleNGet(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	void HandleNSet(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	void HandleNAction(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	void HandleNCreate(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);
	void HandleNDelete(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);

	class NSCU
	{
	protected:
		ServiceBase& service_;
		UID classUID_;
		UINT16 lastMessageID_;

		NSCU(ServiceBase& service, const UID& classUID);
		void readRSP(UINT16& status, DataSet& response, DataSet& data, Command::Code expectedCommand);
	};

	class NEventReportSCU : public NSCU
	{
	public:
		NEventReportSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID, UINT16 eventTypeID, const DataSet& data);
		void writeRQ(const UID& instUID, UINT16 eventTypeID);
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

	class NGetSCU : public NSCU
	{
	public:
		NGetSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID, const std::vector<Tag>& attrList);
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

	class NSetSCU : public NSCU
	{
	public:
		NSetSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID, const DataSet& data);
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

	class NActionSCU : public NSCU
	{
	public:
		NActionSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID, UINT16 actionTypeID, const DataSet& data);
		void writeRQ(const UID& instUID, UINT16 actionTypeID);
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

	class NCreateSCU : public NSCU
	{
	public:
		NCreateSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID, const DataSet& data);
		void writeRQ(const UID& instUID);
		void writeRQ(const DataSet& data);
		void writeRQ();
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

	class NDeleteSCU : public NSCU
	{
	public:
		NDeleteSCU(ServiceBase& service, const UID& classUID);
		void writeRQ(const UID& instUID);
		void readRSP(UINT16& status, DataSet& response, DataSet& data);
	};

#ifdef THIS_ISNT_IMPLEMENTED_YET

			//handler(pdu,rspData,command,data);
	//class NHandler
	//{
	//public:
	//	virtual UINT16 handle(ServiceBase& pdu, dicom::DataSet* rspData_p, dicom::UID& instUID,
	//		const dicom::DataSet& rqCmd, const dicom::DataSet& rqData) = 0;
	//};

	//class NCreateSCP
	//{
	//	NHandler* m_pHandler;
	//public:
	//	NCreateSCP(NHandler* pHandler = 0) : m_pHandler(pHandler) {}
	//	bool handle(ServiceBase& pdu, const dicom::DataSet& rqCmd, const dicom::UID& classUID);
	//};

	void HandleNCreate(NHandlerFunction handler, ServiceBase& pdu,const DataSet& command, const UID& classUID);

	//class NSetSCP
	//{
	//	NHandler* m_pHandler;
	//public:
	//	NSetSCP(NHandler* pHandler = 0) : m_pHandler(pHandler) {}
	//	bool handle(ServiceBase& pdu, const dicom::DataSet& rqCmd, const dicom::UID& classUID);
	//};

	void HandleNSetSCP(NHandlerFunction handler, ServiceBase& pdu, const DataSet& command, const UID& classUID);

	class NCreateSCU  
	{
		const dicom::UID m_classUID;
	public:
		NCreateSCU(const dicom::UID& classUID);
		void writeRQ(ServiceBase& pdu, const dicom::UID& instUID, const dicom::DataSet& data);
		void writeRQ(ServiceBase& pdu, const dicom::UID& instUID);
		void writeRQ(ServiceBase& pdu, const dicom::DataSet& data);
		void writeRQ(ServiceBase& pdu);
		bool readRSP(UINT16& status, dicom::DataSet& data, ServiceBase& pdu);
		bool readRSP(UINT16& status, dicom::DataSet& response, dicom::DataSet& data, ServiceBase& pdu);
	};

	class NSetSCU  
	{
		const dicom::UID m_classUID;
	public:
		NSetSCU(const dicom::UID& classUID);
		void writeRQ(ServiceBase& pdu, const dicom::UID& instUID, const dicom::DataSet& data);
		bool readRSP(UINT16& status, dicom::DataSet& data, ServiceBase& pdu);
		bool readRSP(UINT16& status, dicom::DataSet& response, dicom::DataSet& data, ServiceBase& pdu);
	};
#endif //THIS_ISNT_IMPLEMENTED_YET

}//namespace dicom


#endif //NDIMSE_HPP_INCLUDE_GUARD_5834956123
