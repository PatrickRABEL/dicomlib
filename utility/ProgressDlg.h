//  CProgressDlg.h: interface for the CProgressDlg.h class.
//
/////////////////////////////////////////////////////////////////////////////////////////
#if !defined(WTL_PROGRESSDLG_H_84058038_12A6_4B57_8D4DEE8CDAD3CC59_DEFINED)
#define WTL_PROGRESSDLG_H_84058038_12A6_4B57_8D4DEE8CDAD3CC59_DEFINED

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#include <atlframe.h> // for CDialogResize<>
#include <atlddx.h>   // for DDX
#include <atlctrls.h> // for member controls

using namespace std;


namespace utility
{
	class CProgressDlg: public CDialogImpl<CProgressDlg>
						, public CUpdateUI<CProgressDlg>
						, public CMessageFilter, public CIdleHandler
						//, public CWinDataExchange<CProgressDlg> //  DDX implementation, call DoDataExchange() where relevant.
	{
		protected:
			typedef CProgressDlg thisClass;
			typedef CDialogImpl<CProgressDlg> baseClass;

			CProgressBarCtrl			m_progbar; // 
			CButton						m_CancelButton;
			bool						m_bCancelled;
			/*CWindow**/	HWND				m_wRenenableWnd;

		public:

			enum {IDD = IDD_DLG_PROGRESS};

			BOOL PreTranslateMessage(MSG* pMsg)
			{
				return this->IsDialogMessage(pMsg);
			}

			BOOL OnIdle()
			{
				 UIUpdateChildWindows();
				return FALSE;
			}

			BEGIN_MSG_MAP(CProgressDlg)
				MESSAGE_HANDLER(WM_INITDIALOG,	OnInitDialog)
				//COMMAND_ID_HANDLER(IDCANCEL,	OnCancel) // Default "Cancel" Button is removed.
				COMMAND_ID_HANDLER(IDC_BUTTON_CANCEL,		OnCancel)	
				//NOTIFY_HANDLER(IDC_BUTTON_STOP,   NM_HOVER,   OnStop)


				MESSAGE_HANDLER(WM_CLOSE,		OnClose)	// copied from CWorkListPage for Modaless Dlg.
				MESSAGE_HANDLER(WM_DESTROY,		OnDestroy)	// copied from CWorkListPage for Modaless Dlg.
				//MESSAGE_HANDLER(WM_PAINT,		OnPaint)
				//if(uMsg == WM_FORWARDMSG)
				//	if(PreTranslateMessage((LPMSG)lParam)) return TRUE;

				//CHAIN_MSG_MAP(CDialogResize<CProgressDlg>)
			END_MSG_MAP()

			BEGIN_UPDATE_UI_MAP(thisClass)
				// UPDATE_ELEMENT(IDC_BTN_LAYOUT1, UPDUI_MENUPOPUP)
				UPDATE_ELEMENT(IDC_BUTTON_STOP, UPDUI_DEFAULT)
			END_UPDATE_UI_MAP()

		
			// The DDX map assumes relevant variables exist for the controls for which
			// you'll want to use DDX. Some of those are tough to map from a resource: for
			// instance, and edit box, if it's numeric, might be mapped using DDX_INT,
			// DDX_INT_RANGE, DDX_UINT, DDX_UINT_RANGE, DDX_FLOAT, DDX_FLOAT_RANGE...
		
			// As a result, the 'real' DDX implementation is left for a later version.
			BEGIN_DDX_MAP(CProgressDlg)
			//  DDX_TEXT(nID, var)
			//  DDX_TEXT_LEN(nID, var, len)
			//  DDX_INT(nID, var)
			//  DDX_INT_RANGE(nID, var, min, max)
			//  DDX_UINT(nID, var)
			//  DDX_UINT_RANGE(nID, var, min, max)
			//  DDX_FLOAT(nID, var) // (floats are conditionally defined)
			//  DDX_FLOAT_RANGE(nID, var, min, max)
			//  DDX_FLOAT_P(nID, var, precision)
			//  DDX_FLOAT_P_RANGE(nID, var, min, max, precision)
			//  DDX_CONTROL(nID, obj)
			//  DDX_CONTROL_HANDLE(nID, obj)
			//  DDX_CHECK(nID, var)
			//  DDX_RADIO(nID, var)
			END_DDX_MAP()


		LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			ATLTRACE("CProgressDlg::OnInitDialog()\n");
			CenterWindow(GetParent());	

			this->m_progbar				= GetDlgItem(IDC_PROG_FILERD);
			this->m_CancelButton		= GetDlgItem(IDC_BUTTON_CANCEL);

			m_bCancelled=false;

			//Walk up the window chain to find the main parent wnd and disable it. 
			HWND hwnd = this->m_hWnd;
			do {
				CWindow parent = CWindow(::GetParent(hwnd)); //wnd->GetParent();

				// if we have no parent (ie. the main window)
				// or if our parent is disabled, 
				// then this is the window that we will want to remember for reenabling
				if (!parent || !parent.IsWindowEnabled()) {
					m_wRenenableWnd = hwnd;
					::EnableWindow(m_wRenenableWnd, false);
					break;
				}
			
				hwnd = parent.m_hWnd;
			} while (1);

			// Re-enable this window
			EnableWindow(TRUE);

			return 0;
		} // OnInitDialog

		LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			// TODO: Add your implementation code here
			//this->m_progbar.UnsubclassWindow();
			if(::IsWindow(m_hWnd))
			{
				::DestroyWindow(m_hWnd);
			}

			m_bCancelled=true;

			//if (m_bModal)
			{
				//m_bModal = FALSE;
				//CWnd *pMainWnd = AfxGetMainWnd();

				if (m_wRenenableWnd)
				//m_wRenenableWnd->EnableWindow(TRUE);
				::EnableWindow(m_wRenenableWnd,true);
			}

			//EndDialog(wID);
			return 0;
		} // OnIDCancel

		LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
		{
			m_bCancelled=true;

			if(::IsWindow(m_hWnd))
			{
				::DestroyWindow(m_hWnd);
			}
			PostMessage(WM_CLOSE);
			// NOTE: For some reason, DefWindowProc doesn't call DestroyWindow
			//  when handling WM_CLOSE for modeless dialogs, so we'll do it ourself
			bHandled = TRUE;

			{
				//m_bModal = FALSE;
				//CWnd *pMainWnd = AfxGetMainWnd();

				if (m_wRenenableWnd)
				//m_wRenenableWnd->EnableWindow(TRUE);
				::EnableWindow(m_wRenenableWnd,true);
			}

			return 0;
		}

		LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
		{
			//this->m_progbar.UnsubclassWindow();
			//if(::IsWindow(m_hWnd))
			//{
			//	::DestroyWindow(m_hWnd);
			//}
			//PostMessage(WM_CLOSE);

			//if (m_bModal)
			{
				//m_bModal = FALSE;
				//CWnd *pMainWnd = AfxGetMainWnd();

				if (m_wRenenableWnd)
				//m_wRenenableWnd->EnableWindow(TRUE);
				::EnableWindow(m_wRenenableWnd,true);
			}
			// Let anyone else (including DefWindowProc) see the message
			bHandled = FALSE;
			return 0;
		}

		// Message pumping function that can either be used to pump messages during
		// long operations. This version will only pass messages to this window (and
		// all child windows). (Thanks to Michael <mbh-ep@post5.tele.dk> for this)
		void PeekAndPump(/*BOOL bCancelOnESCkey*/ /*= TRUE*/)
		{
			if (/*m_bModal &&*/ ::GetFocus() != m_hWnd)
				SetFocus();

			MSG msg;
			while (!m_bCancelled && ::PeekMessage(&msg, NULL,0,0,PM_NOREMOVE)) 
			{
				if (/*bCancelOnESCkey && */(msg.message == WM_CHAR) && (msg.wParam == VK_ESCAPE))
					m_bCancelled=true;
					//OnCancel();

				// Cancel button disabled if modal, so we fake it.
				if (/*m_bModal &&*/ (msg.message == WM_LBUTTONUP))
				{
					CRect rect;
					m_CancelButton.GetWindowRect(rect);
					if (rect.PtInRect(msg.pt))
						m_bCancelled=true;
						//OnCancel();
				}

				if (!::GetMessage(&msg, NULL,0,0))//(!AfxGetApp()->PumpMessage()) 
				{
					::PostQuitMessage(0);
					return;
				} 
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		bool Cancelled()
		{
			return m_bCancelled;
		}

		void OnAutofire(int pos)
		{
			static UINT nPos = 0;

			// increment the progress control
			//if (wID == IDAUTOFIRE)
				m_progbar.SetPos(pos); // ++nPos);
		}

		void SetRange(int nMin, int nMax)
		{
			m_progbar.SetRange ( nMin, nMax ); 
		}
		void SetPos(int pos)
		{
			m_progbar.SetPos ( pos ); 
		}
		void SetStep(int step)
		{
			m_progbar.SetStep ( step );
		}

		//m_progbar.SetBarColor(::GetSysColor(COLOR_HIGHLIGHT));//COLOR_HIGHLIGHT

		void StepIt()
		{
			m_progbar.StepIt(); 	
		}

	}; // class CProgressDlg
}

#endif // !defined(WTL_PROGRESSDLG_H_84058038_12A6_4B57_8D4DEE8CDAD3CC59_DEFINED)
