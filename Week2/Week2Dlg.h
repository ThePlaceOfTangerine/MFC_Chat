
// Week2Dlg.h : header file
//

#pragma once
#include <map>
#include <vector>

// CWeek2Dlg dialog
class CWeek2Dlg : public CDialogEx
{
// Construction
public:
	CWeek2Dlg(CWnd* pParent = nullptr);	// standard constructor
	CString m_strUser;

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_WEEK2_DIALOG };
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

	void InitTree();
	void InitFakeData();
	void ShowChat();
	void AppendMessage(BOOL bMe, LPCTSTR name, LPCTSTR text);
	void AddMessage(LPCTSTR time, LPCTSTR sender, LPCTSTR msg);
	HTREEITEM AddTreeItem(HWND hTree, HTREEITEM hParent, LPCTSTR text);

	struct ChatMsg { CString time, sender, text; };
	std::map<CString, std::vector<ChatMsg>> m_chats;   // tên người gửi -> các tin nhắn
	CString m_strCurrent;                              // người nhận

	afx_msg void OnTvnSelchangedTreeFriends(NMHDR* pNMHDR, LRESULT* pResult);
public:
	afx_msg void OnBnClickedBtnSend();
};
