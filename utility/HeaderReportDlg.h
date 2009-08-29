// HeaderReportDlg.h : interface of the HeaderReportDlg class
// to do: PixelSpacing printed two lines
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <atlbase.h>
#include <atlapp.h>
#include <atlwin.h>

#include <iostream>
#include <iomanip>
#include "dicomlib/DataSet.hpp"

namespace utility
{
	void AddDataSetToReport(std::string* report, const dicom::DataSet& data, bool is_sequence);

	//
	void replaceNewline(std::string& text)
	{
		int where=0;
		for(;;)
		{
			where=text.find("\n",where);;
			if(std::string::npos==where)
				break;
			text.replace(where, 1, ",");
			text.insert(where+1, " ");
			where+=2;
		}
	}

	void AddItemToReport(std::string* report, const dicom::DataSet::value_type& item, bool is_sequence)
	{
		dicom::Tag tag=item.first;

		//to show Tag in hex before the name
		std::ostringstream os;
		os << "(0x" << std::hex << std::setfill('0') << std::setw(4)<< dicom::GroupTag(tag) << "," 
			<< std::setw(4) << dicom::ElementTag(tag) << std::dec << ")    " ;

		if(dicom::GetName(tag).size()<11)
			os << dicom::GetName(tag) << "\t\t\t";
		else
			os << /*std::setw(40) << std::setfill(' ') <<*/ dicom::GetName(tag) << "\t\t";

		const dicom::Value& value=item.second;

		//to show value in the line with tag and name
		std::ostringstream os2;	
		os2 << value;
		std::string value_str = os2.str();

		if( value_str=="Sequence" || value_str=="sequence" || value_str=="SEQUENCE"||
			value_str=="SQ,Sequence" || value_str=="SQ,sequence" || value_str=="SQ,SEQUENCE")
		{
			dicom::Sequence sequence= item.second.Get<dicom::Sequence>();
			//item.second >> sequence;//the same as above

			if(is_sequence==true)
				*report  = *report + "      <";

			*report = *report + os.str() + value_str + std::string("\n");

			if(!sequence.empty())
			{
				for(int i=0; i<sequence.size(); i++)
				{
					if(!sequence[i].empty())
					{
						*report = *report + "      <";
						AddDataSetToReport(report, sequence[i], true);
					}
				}
			}
		}
		else
		{
			unsigned short index=value_str.find(',', 0);
			if(index+1 < value_str.size())
				value_str = value_str.substr(index+1);
			else
				value_str = "";

			replaceNewline(value_str);

			if(is_sequence==true && ((*report).rfind('<')!= (*report).size()-1))
				*report = *report + "      <";

			*report = *report + os.str() + value_str + std::string("\n");
		}

	}


	void AddDataSetToReport(std::string* report, const dicom::DataSet& data, bool sequence)
	{
		std::for_each(
			data.begin(),
			data.end(),
			boost::bind(AddItemToReport, report, _1, sequence));
		
	}



	class HeaderReportDlg : public CDialogImpl<HeaderReportDlg>,
							public CDialogResize<HeaderReportDlg>
	{
		
	public:
		std::string text_;
		enum { IDD = IDD_DIALOG_HEADERREPORT};

		BEGIN_MSG_MAP(TestDialog)
			MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
			COMMAND_ID_HANDLER(IDOK, OnCloseCmd)
			COMMAND_ID_HANDLER(IDCANCEL, OnCloseCmd)
			CHAIN_MSG_MAP(CDialogResize<HeaderReportDlg>)
		END_MSG_MAP()

		BEGIN_DLGRESIZE_MAP(HeaderReportDlg)
			BEGIN_DLGRESIZE_GROUP()
				DLGRESIZE_CONTROL(IDC_EDIT_HEADERREPORT,	DLSZ_SIZE_X | DLSZ_SIZE_Y)
				DLGRESIZE_CONTROL(IDOK,						DLSZ_MOVE_X | DLSZ_MOVE_Y)
			END_DLGRESIZE_GROUP()
		END_DLGRESIZE_MAP()

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

		void InsertCarriageReturns(std::string& text)
		{
			int where=0;
			for(;;)
			{
				where=text.find("\n",where);;
				if(std::string::npos==where)
					break;
				text.insert(where,"\r");
				where+=2;
			}
		}

		LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			DlgResize_Init();

			//CenterWindow(GetParent());

			//CRichEditCtrl edit(GetDlgItem(IDC_EDIT_HEADERREPORT));
			CEdit edit(GetDlgItem(IDC_EDIT_HEADERREPORT));

			InsertCarriageReturns(text_);

			edit.SetWindowText(text_.c_str());

			return TRUE;
		}

		LRESULT OnCloseCmd(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			EndDialog(wID);
			return 0;
		}

	};
}