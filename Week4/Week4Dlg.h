
// Week4Dlg.h : header file
//

#pragma once

#define WM_NET_CONNECTED  (WM_APP + 1)   // wParam = SOCKET
#define WM_NET_MESSAGE    (WM_APP + 2)   // lParam = CString*
#define WM_NET_CLOSED     (WM_APP + 3)
#define WM_NET_ERROR      (WM_APP + 4)   // wParam = mã lỗi

// CWeek4Dlg dialog
class CWeek4Dlg : public CDialogEx
{
// Construction
public:
	CWeek4Dlg(CWnd* pParent = nullptr);	// standard constructor

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WEEK4_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support


// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

	SOCKET m_listen = INVALID_SOCKET;
	SOCKET m_sock = INVALID_SOCKET;

	struct NetParam { CWeek4Dlg* dlg; bool server; CString ip; int port; };
	static UINT NetThread(LPVOID pParam);
	void StartNet(bool server);
	void Disconnect();
	void AppendMessage(BOOL bMe, LPCTSTR name, LPCTSTR text);

	afx_msg LRESULT OnNetConnected(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnNetMessage(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnNetClosed(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnNetError(WPARAM wParam, LPARAM lParam);
	afx_msg void OnBnClickedBtnListen();
	afx_msg void OnBnClickedBtnConnect();
	afx_msg void OnBnClickedBtnDisconnect();
	afx_msg void OnBnClickedBtnSend();
	afx_msg void OnDestroy();
};
