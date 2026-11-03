
// MFCChatDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "MFCChat.h"
#include "MFCChatDlg.h"
#include "afxdialogex.h"
#include "Database.h"
#include <atlconv.h>
#include <vector>

static const char* DATABASE_PATH = "D:\\Work\\CMC\\MFCChat\\chat.db";

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMFCChatDlg dialog



CMFCChatDlg::CMFCChatDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_MFCCHAT_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMFCChatDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_CONVERSATION, m_conversationList);
}

BEGIN_MESSAGE_MAP(CMFCChatDlg, CDialogEx)
	ON_WM_COPYDATA()
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON_SEND, &CMFCChatDlg::OnBnClickedButtonSend)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CONVERSATION, &CMFCChatDlg::OnLvnItemchangedListConversation)
END_MESSAGE_MAP()


// CMFCChatDlg message handlers

BOOL CMFCChatDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	CString title;
	title.Format(L"Chat - %s", m_currentUser);

	SetWindowText(title);

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	// TODO: Add extra initialization here

	m_conversationList.DeleteAllItems();
	m_fakeChats.RemoveAll();

	m_conversationList.ModifyStyle(
		LVS_TYPEMASK,
		LVS_REPORT
	);

	m_conversationList.SetExtendedStyle(
		m_conversationList.GetExtendedStyle() |
		LVS_EX_FULLROWSELECT
	);

	while (m_conversationList.DeleteColumn(0))
	{
	}

	m_conversationList.InsertColumn(
		0, L"Ng??i dùng", LVCFMT_LEFT, 100);

	m_conversationList.InsertColumn(
		1, L"Tin nh?n cu?i", LVCFMT_LEFT, 180);

	m_conversationList.InsertColumn(
		2, L"Th?i gian", LVCFMT_LEFT, 120);

	Database db;

	if (!db.Open(DATABASE_PATH))
	{
		AfxMessageBox(L"Cannot open chat database.");
		return FALSE;
	}

	CT2A currentUser(m_currentUser, CP_UTF8);
	vector<ConversationSummary> conversations;

	bool loaded = db.GetConversationSummaries(
		currentUser,
		conversations
	);

	db.Close();

	if (!loaded)
	{
		AfxMessageBox(L"Cannot load conversation list.");
		return FALSE;
	}

	for (const auto& conversation : conversations)
	{
		CA2T usernameText(conversation.username.c_str(), CP_UTF8);
		CA2T messageText(conversation.lastMessage.c_str(), CP_UTF8);
		CA2T timeText(conversation.lastSentAt.c_str(), CP_UTF8);

		CString preview(static_cast<LPCTSTR>(messageText));
		preview.Replace(L"\r", L" ");
		preview.Replace(L"\n", L" ");

		if (preview.GetLength() > 35)
			preview = preview.Left(32) + L"...";

		int index = m_conversationList.InsertItem(
			m_conversationList.GetItemCount(),
			static_cast<LPCTSTR>(usernameText)
		);

		m_conversationList.SetItemText(
			index, 1, preview
		);

		m_conversationList.SetItemText(
			index, 2, static_cast<LPCTSTR>(timeText)
		);

		m_fakeChats[static_cast<LPCTSTR>(usernameText)] = preview;
	}

	CString messageName;
	messageName.Format(L"MFC_CHAT_USER_%s", m_currentUser);

	m_chatMessageId = RegisterWindowMessage(messageName);

	if (m_conversationList.GetItemCount() > 0)
	{
		m_conversationList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

		m_currentChatUser = m_conversationList.GetItemText(0, 0);

		LoadChatHistory();
	}

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CMFCChatDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMFCChatDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CMFCChatDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}




void CMFCChatDlg::OnBnClickedButtonSend()
{
	CString message;
	GetDlgItemText(IDC_EDIT_MESSAGE, message);
	message.Trim();

	if (message.IsEmpty())
		return;

	if (m_currentChatUser.IsEmpty())
	{
		AfxMessageBox(L"Please select a conversation first.");
		return;
	}

	if (!SendChatMessage(message))
	{
		AfxMessageBox(L"Cannot find the recipient's chat window.");
		return;
	}

	Database db;

	if (!db.Open(DATABASE_PATH))
	{
		AfxMessageBox(L"Cannot open chat database.");
		return;
	}

	CT2A sender(m_currentUser, CP_UTF8);
	CT2A receiver(m_currentChatUser, CP_UTF8);
	CT2A content(message, CP_UTF8);

	bool saved = db.SaveMessage(sender, receiver, content);

	db.Close();

	if (!saved)
	{
		AfxMessageBox(L"Message was sent, but could not be saved to database.");
	}
	else
	{
		RefreshConversationList();

		CString targetTitle;
		targetTitle.Format(L"Chat - %s", m_currentChatUser.GetString());

		HWND hTarget = ::FindWindow(nullptr, targetTitle);

		if (hTarget != nullptr && m_refreshMessageId != 0)
		{
			::SendMessage(
				hTarget,
				m_refreshMessageId,
				0,
				0
			);
		}
	}

	LoadChatHistory();
	SetDlgItemText(IDC_EDIT_MESSAGE, L"");
}

void CMFCChatDlg::OnLvnItemchangedListConversation(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);

	if ((pNMLV->uChanged & LVIF_STATE) &&
		(pNMLV->uNewState & LVIS_SELECTED) &&
		!(pNMLV->uOldState & LVIS_SELECTED))
	{
		int index = pNMLV->iItem;

		if (index >= 0)
		{
			m_currentChatUser = m_conversationList.GetItemText(index, 0);
			SetDlgItemText(IDC_STATIC_CHAT_USER, m_currentChatUser);

			LoadChatHistory();
		}
	}

	*pResult = 0;
}


BOOL CMFCChatDlg::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	if (pCopyDataStruct == nullptr ||
		pCopyDataStruct->dwData != 1 ||
		pCopyDataStruct->lpData == nullptr ||
		pCopyDataStruct->cbData < sizeof(wchar_t) ||
		pCopyDataStruct->cbData % sizeof(wchar_t) != 0)
	{
		return FALSE;
	}

	const wchar_t* rawData = static_cast<const wchar_t*>(pCopyDataStruct->lpData);

	size_t charCount = pCopyDataStruct->cbData / sizeof(wchar_t);

	if (rawData[charCount - 1] != L'\0')
		return FALSE;

	CString data(rawData);

	int separator = data.Find(L'|');

	if (separator < 1)
		return FALSE;

	CString sender = data.Left(separator);
	CString message = data.Mid(separator + 1);

	CString displayMessage;
	displayMessage.Format(L"%s: %s\r\n", sender.GetString(), message.GetString());

	m_messageHistory += displayMessage;

	SetDlgItemText(IDC_RICHEDIT_MESSAGE, m_messageHistory);
	RefreshConversationList();

	return TRUE;
}

void CMFCChatDlg::SetCurrentUser(const CString& username)
{
	m_currentUser = username;
}

CString CMFCChatDlg::GetCurrentUser() const
{
	return m_currentUser;
}

LRESULT CMFCChatDlg::WindowProc(
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	if (m_chatMessageId != 0 &&
		message == m_chatMessageId)
	{
		return reinterpret_cast<LRESULT>(GetSafeHwnd());
	}

	if (m_refreshMessageId != 0 &&
		message == m_refreshMessageId)
	{
		RefreshConversationList();
		return 0;
	}

	return CDialogEx::WindowProc(message, wParam, lParam);
}

BOOL CMFCChatDlg::SendChatMessage(const CString& message)
{
	if (m_currentChatUser.IsEmpty() || message.IsEmpty())
		return FALSE;

	CString targetTitle;
	targetTitle.Format(L"Chat - %s", m_currentChatUser.GetString());

	HWND hTarget = ::FindWindow(nullptr, targetTitle);

	if (hTarget == nullptr)
		return FALSE;

	CString payload;
	payload.Format(L"%s|%s", m_currentUser.GetString(), message.GetString());

	COPYDATASTRUCT cds{};
	cds.dwData = 1;
	cds.cbData = static_cast<DWORD>((payload.GetLength() + 1) * sizeof(wchar_t));
	cds.lpData = const_cast<wchar_t*>(payload.GetString());

	return ::SendMessage(
		hTarget,
		WM_COPYDATA,
		reinterpret_cast<WPARAM>(GetSafeHwnd()),
		reinterpret_cast<LPARAM>(&cds)
	) != FALSE;
}


void CMFCChatDlg::LoadChatHistory()
{
	m_messageHistory.Empty();

	if (m_currentChatUser.IsEmpty())
	{
		SetDlgItemText(IDC_RICHEDIT_MESSAGE, L"");
		return;
	}

	Database db;

	if (!db.Open(DATABASE_PATH))
	{
		AfxMessageBox(L"Cannot open chat database.");
		SetDlgItemText(IDC_RICHEDIT_MESSAGE, L"");
		return;
	}

	CT2A currentUser(m_currentUser, CP_UTF8);
	CT2A chatUser(m_currentChatUser, CP_UTF8);

	std::vector<MessageRecord> messages;

	bool loaded = db.LoadMessages(currentUser, chatUser, messages);

	db.Close();

	if (!loaded)
	{
		AfxMessageBox(L"Cannot load chat history.");
		SetDlgItemText(IDC_RICHEDIT_MESSAGE, L"");
		return;
	}

	for (const auto& message : messages)
	{
		CA2T sender(message.senderUsername.c_str(), CP_UTF8);
		CA2T content(message.content.c_str(), CP_UTF8);
		CA2T sentAt(message.sentAt.c_str(), CP_UTF8);

		CString displayMessage;
		displayMessage.Format(
			L"[%s] %s: %s\r\n",
			static_cast<LPCTSTR>(sentAt),
			static_cast<LPCTSTR>(sender),
			static_cast<LPCTSTR>(content)
		);

		m_messageHistory += displayMessage;
	}

	SetDlgItemText(IDC_RICHEDIT_MESSAGE, m_messageHistory);
}


void CMFCChatDlg::RefreshConversationList()
{
	CString selectedUser = m_currentChatUser;

	Database db;

	if (!db.Open(DATABASE_PATH))
		return;

	CT2A currentUser(m_currentUser, CP_UTF8);
	std::vector<ConversationSummary> conversations;

	bool loaded = db.GetConversationSummaries(
		currentUser,
		conversations
	);

	db.Close();

	if (!loaded)
		return;

	m_conversationList.DeleteAllItems();
	m_fakeChats.RemoveAll();

	CT2A selectedUserUtf8(selectedUser, CP_UTF8);
	int selectedIndex = -1;

	for (const auto& conversation : conversations)
	{
		CA2T username(conversation.username.c_str(), CP_UTF8);
		CA2T content(conversation.lastMessage.c_str(), CP_UTF8);
		CA2T sentAt(conversation.lastSentAt.c_str(), CP_UTF8);

		CString preview(static_cast<LPCTSTR>(content));
		preview.Replace(L"\r", L" ");
		preview.Replace(L"\n", L" ");

		if (preview.GetLength() > 35)
			preview = preview.Left(32) + L"...";

		int index = m_conversationList.InsertItem(
			m_conversationList.GetItemCount(),
			static_cast<LPCTSTR>(username)
		);

		m_conversationList.SetItemText(index, 1, preview);
		m_conversationList.SetItemText(
			index, 2, static_cast<LPCTSTR>(sentAt));

		m_fakeChats[static_cast<LPCTSTR>(username)] = preview;

		if (conversation.username ==
			static_cast<LPCSTR>(selectedUserUtf8))
		{
			selectedIndex = index;
		}
	}

	if (selectedIndex >= 0)
	{
		m_conversationList.SetItemState(selectedIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED
		);

		m_conversationList.EnsureVisible(
			selectedIndex, FALSE);
	}
}
