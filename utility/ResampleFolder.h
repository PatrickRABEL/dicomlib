#include <sstream>
#include "utility/ProgressDlg.h"

namespace utility
{
	void ResampleFolder(LPCTSTR pFolder, HWND m_hWnd)
	{
		//CWaitCursor waitcursor;
		std::ostringstream errors;

		TCHAR szFullPathFileName[MAX_PATH];
		TCHAR newFullPathFileName2[MAX_PATH];
		WIN32_FIND_DATA FileData = {0};
		bool bFinished = FALSE; 
		unsigned int count=0;

		_stprintf(szFullPathFileName, TEXT("%s\\*.*"), pFolder);
		HANDLE hSearch = FindFirstFile(szFullPathFileName, &FileData); 
		if (hSearch == INVALID_HANDLE_VALUE) 
			return ;

		while (!bFinished)
		{	
			count++;
			if (!FindNextFile(hSearch, &FileData)) 
				if (GetLastError() == ERROR_NO_MORE_FILES) 
					bFinished = TRUE;
		}

		int nMin = 0;  int nMax = count;  // Progress bar initial range
		CProgressDlg dlg; //( 0 ); 
		dlg.Create(m_hWnd);
		//if(dlg.Create(m_hWnd) == NULL)
		//{
		//	ATLTRACE(_T("Progress dialog creation failed!\n"));
		//	return ;
		//}
		dlg.ShowWindow(SW_SHOWNA);
		
		dlg.SetRange ( nMin, nMax ); 
		dlg.SetPos ( nMin );   
		dlg.SetStep ( 1 );
		//m_progress.SetBarColor(::GetSysColor(COLOR_HIGHLIGHT));//COLOR_HIGHLIGHT


		//restart from the first file
		bFinished = FALSE; 
		_stprintf(szFullPathFileName, TEXT("%s\\*.*"), pFolder);
		hSearch = FindFirstFile(szFullPathFileName, &FileData); 
		if (hSearch == INVALID_HANDLE_VALUE) 
			return ;

		_stprintf(newFullPathFileName2, TEXT("%s\\ReSampled\\"), pFolder);
		BOOL bOK = CreateDirectory(newFullPathFileName2, NULL);

		while (!bFinished)
		{
			lstrcpy(szFullPathFileName, pFolder);
			lstrcat(szFullPathFileName, _T("\\"));
			lstrcat(szFullPathFileName, FileData.cFileName);

			if (!(FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				string filename=FileData.cFileName;
				transform(filename.begin(), filename.end(),  filename.begin(), tolower);

				if( (filename.rfind(".bmp")!=string::npos)||
					(filename.rfind(".iv")!=string::npos)||(filename.rfind("iv2")!=string::npos) )
				{
					lstrcat(newFullPathFileName2, FileData.cFileName);
					CopyFile(szFullPathFileName, newFullPathFileName2, true);
				}
				else
				{
					try
					{
						//CWaitCursor waitcursor;

						dicom::DataSet ds2;
						ifstream In(szFullPathFileName,ios::binary);
						dicom::ReadFromStream(In,ds2);
						ResampleImage(ds2);

						string dsfilename=FileData.cFileName;
						if(filename.rfind(".dcm")==string::npos)
							dsfilename += "_ds";
						else
							dsfilename=dsfilename.insert(filename.rfind(".dcm"), "_ds");

						lstrcat(newFullPathFileName2, dsfilename.c_str());
						dicom::TS transfer_syntax(dicom::IMPL_VR_LE_TRANSFER_SYNTAX);
						dicom::Write(ds2, newFullPathFileName2, transfer_syntax);
					}
					catch(dicom::TagNotFound& e)
					{
						lstrcat(newFullPathFileName2, FileData.cFileName);
						CopyFile(szFullPathFileName, newFullPathFileName2, true);
						string mytag=dicom::GetName(e.tag_) + dicom::GetTagString(e.tag_) + " Tag not found";
						errors << mytag << " in " << FileData.cFileName << "\n";
					}
					catch(std::exception& e)
					{
						lstrcat(newFullPathFileName2, FileData.cFileName);
						CopyFile(szFullPathFileName, newFullPathFileName2, true);
						errors << e.what() << " in " << FileData.cFileName << "\n";
					}
				}
			}

			//dlg.StepIt();

			//Not ready. Nov 24, 2006
			if(::IsWindow(dlg.m_hWnd))
			{
				dlg.StepIt();
				dlg.PeekAndPump();
				if (dlg.Cancelled()) 
					MessageBox(NULL, "Progress Cancelled", 0, 0);
			}
			else
				return;

			_stprintf(newFullPathFileName2, TEXT("%s\\ReSampled\\"), pFolder);

			if (!FindNextFile(hSearch, &FileData)) 
			{
				if (GetLastError() == ERROR_NO_MORE_FILES) 
					bFinished = TRUE;
			} 
		}

		FindClose(hSearch);
		if(::IsWindow(dlg.m_hWnd))
			dlg.DestroyWindow();

		lstrcat(newFullPathFileName2, "ResampleLog.txt");
		std::ofstream out(newFullPathFileName2);
		out << errors.str();
	}
}