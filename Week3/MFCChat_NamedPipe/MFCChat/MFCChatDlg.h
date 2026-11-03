// MFCChatDlg.h : header file
//

#pragma once

#include "NamedPipeServer.h"
#include "NamedPipeClient.h"

// CMFCChatDlg dialog
class CMFCChatDlg : public CDialogEx
{
	// Construction
public:
	CMFCChatDlg(CWnd* pParent = nullptr);

	void SetCurrentUser(const CString& username);
	CString GetCurrentUser() const;

	BOOL SendChatMessage(const CString& message);

	void RefreshConversationList();

	afx_msg void OnBnClickedButtonSend();
	afx_msg void OnLvnItemchangedListConversation(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnPipeMessage(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	// Implementation
	HICON m_hIcon;

	virtual BOOL OnInitDialog();

	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();

	CListCtrl m_conversationList;
	CMapStringToString m_fakeChats;

	CRichEditCtrl m_richEditMessage;

	DECLARE_MESSAGE_MAP()

private:
	CString m_currentUser;
	CString m_currentChatUser;
	CString m_messageHistory;
	void LoadChatHistory();

	NamedPipeServer m_pipeServer;
	NamedPipeClient m_pipeClient;

	HANDLE m_roleMutex = nullptr;
	bool m_isPipeServer = false;

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MFCCHAT_DIALOG };
#endif
};