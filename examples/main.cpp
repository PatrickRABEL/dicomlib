#include <iostream>
#include "dicomlib/dicomlib.hpp"
/*
	modify these constants as appropriate for your environment
*/

const std::string host = "localhost";
const std::string local_AE = "client";
const std::string remote_AE = "server";
const short remote_port = 104;		//standard dicom port

namespace demo
{
	void OpenDataSet(std::string filename)
	{
		dicom::DataSet data;
		dicom::Read(filename,data);

        std::cout << "File Opened" << std::endl;
		//std::cout << data << std::endl;


		std::string PatientID;

		data(dicom::TAG_PAT_ID) >> PatientID;

		std::cout << "Patient ID is " << PatientID << std::endl;

		std::cout << "Pixel data is " <<  data(dicom::TAG_PIXEL_DATA) << std::endl;

		
		//We can access underlying pixel data as follows:
		std::vector<unsigned short> PixelData;

		data(dicom::TAG_PIXEL_DATA) >> PixelData;
	}

	void ConnectToRemoteServer()
	{
		dicom::PresentationContexts presentation_contexts;
		presentation_contexts.Add(dicom::MAMMO_PRES_IMAGE_STORAGE_SOP_CLASS);
		presentation_contexts.Add(dicom::MAMMO_PROC_IMAGE_STORAGE_SOP_CLASS);
		//add further presentation contexts as appropriate...
		dicom::ClientConnection connection(host,remote_port,local_AE,remote_AE,presentation_contexts);
	}

	void PrintResult(const dicom::DataSet& data)
	{
		std::cout << "Query result:" << std::endl << data << std::endl;
	}
	void QueryRemoteServer()
	{
		dicom::PresentationContexts presentation_contexts;
		presentation_contexts.Add(dicom::STUDY_ROOT_QR_FIND_SOP_CLASS);
	
		dicom::ClientConnection connection(host,remote_port,local_AE,remote_AE,presentation_contexts);
		
		
		//build a query to search for all studies on patients names beginning with "A"
		dicom::DataSet query;
		query.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL,std::string("STUDY"));
		query.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID,dicom::UID("*"));
		query.Put<dicom::VR_PN>(dicom::TAG_PAT_NAME,std::string("A*"));
		
		//execute the query
		std::vector<dicom::DataSet> result = connection.Find(query,dicom::QueryRetrieve::STUDY_ROOT);

		//print the results
		std::for_each(result.begin(),result.end(),PrintResult);
	}

	/*!
		Instruct the pacs server to send the set of images belonging to
        a given study to another DICOM application entity
	*/
	void DoCMove()
	{
		dicom::PresentationContexts presentation_contexts;
		presentation_contexts.Add(dicom::STUDY_ROOT_QR_MOVE_SOP_CLASS);
		presentation_contexts.Add(dicom::PATIENT_ROOT_QR_MOVE_SOP_CLASS);

		dicom::ClientConnection connection(host,remote_port,local_AE,remote_AE,presentation_contexts);
		
		//make CMove request
		dicom::DataSet request;
		request.Put<dicom::VR_CS>(dicom::TAG_QR_LEVEL,std::string("STUDY"));
		request.Put<dicom::VR_UI>(dicom::TAG_STUDY_INST_UID,dicom::UID("1.2.840.113619.2.66.2159378590.19003000418141451.20000"));

		//request that a set of images be sent.
		dicom::DataSet result=connection.Move("Client,",request,dicom::QueryRetrieve::STUDY_ROOT);
		std::cout << result << std::endl;
	}
}//namespace demo

int main(int, char**)
{
	const std::string filename="test.dcm";			//modify this as appropriate

	try
	{

        std::cout << "*****************************" << std::endl 
        	      << "* dicomlib demo application *" << std::endl
        	      << "*****************************" << std::endl;

        std::cout << std::endl << "Attempting to open a data set from " << filename << std::endl;
		demo::OpenDataSet(filename);

        std::cout << std::endl << "Attempting to connect to the remote server at " << host << ":" << remote_port << std::endl;
		demo::ConnectToRemoteServer();

        std::cout <<  std::endl << "Attempting to query the remote server..." << std::endl;
		demo::QueryRemoteServer();
	}
	catch(std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}
	return 0;
}
