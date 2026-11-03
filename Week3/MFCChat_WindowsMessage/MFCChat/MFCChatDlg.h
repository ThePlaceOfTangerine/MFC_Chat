// MFCChatDlg.h : header file
//

#pragma once

#include "ChatWindowsMessage.h"


// CMFCChatDlg dialog
class CMFCChatDlg : public CDialogEx
{
	// Construction
public:
	CMFCChatDlg(CWnd* pParent = nullptr);

	void SetCurrentUser(const CString& username);
	CString GetCurrentUser() const;

	afx_msg void OnBnClickedButtonSend();
	afx_msg void OnLvnItemchangedListConversation(NMHDR* pNMHDR, LRESULT* pResult);

	BOOL SendChatMessage(const CString& message);

	UINT m_chatMessageId = 0;

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	void RefreshConversationList();
	UINT m_refreshMessageId = 0;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	// Implementation
	HICON m_hIcon;

	virtual BOOL OnInitDialog();

	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();

	// Receive Windows Message
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);

	CListCtrl m_conversationList;
	CMapStringToString m_fakeChats;

	DECLARE_MESSAGE_MAP()

private:
	CString m_currentUser;
	CString m_currentChatUser;
	CString m_messageHistory;
	void LoadChatHistory();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MFCCHAT_DIALOG };
#endif
};