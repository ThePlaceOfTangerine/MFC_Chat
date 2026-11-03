// MFCChatDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "MFCChat.h"
#include "MFCChatDlg.h"
#include "afxdialogex.h"
#include <atlconv.h>
#include <vector>
#include "TcpChatClient.h"
#include <string>
#include "json.hpp"
#include <cstdio>

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
	ON_MESSAGE(WM_APP_TCP_MESSAGE, &CMFCChatDlg::OnTcpMessage)
	ON_MESSAGE(WM_APP_TCP_RESPONSE, &CMFCChatDlg::OnTcpResponse)
END_MESSAGE_MAP()

// CMFCChatDlg message handlers

BOOL CMFCChatDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	m_tcpClient.SetNotifyWindow(m_hWnd);

	CString title;
	title.Format(L"Chat - %s", m_currentUser.GetString());

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
		0, L"Người dùng", LVCFMT_LEFT, 100);

	m_conversationList.InsertColumn(
		1, L"Tin nhắn cuối", LVCFMT_LEFT, 180);

	m_conversationList.InsertColumn(
		2, L"Thời gian", LVCFMT_LEFT, 120);

	CString messageName;
	messageName.Format(L"MFC_CHAT_USER_%s", m_currentUser.GetString());

	m_chatMessageId = RegisterWindowMessage(messageName);

	if (m_conversationList.GetItemCount() > 0)
	{
		m_conversationList.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

		m_currentChatUser = m_conversationList.GetItemText(0, 0);

		LoadChatHistory();
	}

	CT2A usernameUtf8(m_currentUser, CP_UTF8);
	CT2A passwordUtf8(m_currentPassword, CP_UTF8);

	if (!m_tcpClient.Connect("127.0.0.1", 5000))
	{
		AfxMessageBox(L"Cannot connect to TCP server.");
	}
	else if (!m_tcpClient.Login(
		static_cast<const char*>(usernameUtf8),
		static_cast<const char*>(passwordUtf8)))
	{
		std::string errorMessage = m_tcpClient.GetLastErrorMessage();

		m_tcpClient.Disconnect();

		CA2T errorText(errorMessage.c_str(), CP_UTF8);

		CString message;
		message.Format(
			L"TCP login failed: %s",
			errorMessage.empty()
			? L"Unknown error"
			: static_cast<LPCTSTR>(errorText));

		AfxMessageBox(message);
	}
	else
	{
		// Đăng nhập TCP thành công: lấy danh sách người dùng/hội thoại ngay.
		// Trước đây không có chỗ nào gọi hàm này sau khi login, nên danh sách
		// luôn trống cho tới khi có tin nhắn đến.
		RefreshConversationList();
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
		AfxMessageBox(
			L"Please select a conversation first."
		);

		return;
	}

	if (!m_tcpClient.IsConnected())
	{
		AfxMessageBox(
			L"Not connected to TCP server."
		);

		return;
	}

	CT2A receiverUtf8(m_currentChatUser, CP_UTF8);
	CT2A contentUtf8(message, CP_UTF8);

	std::string receiver(
		static_cast<const char*>(receiverUtf8)
	);

	std::string content(
		static_cast<const char*>(contentUtf8)
	);

	if (!m_tcpClient.SendMessageTo(receiver, content))
	{
		AfxMessageBox(
			L"Failed to send message through TCP."
		);

		return;
	}

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

void CMFCChatDlg::SetCurrentPassword(const CString& password)
{
	m_currentPassword = password;
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
	if (m_currentChatUser.IsEmpty())
	{
		m_messageHistory.Empty();
		SetDlgItemText(IDC_RICHEDIT_MESSAGE, L"");
		return;
	}

	if (!m_tcpClient.IsConnected())
		return;

	CT2A usernameUtf8(m_currentChatUser, CP_UTF8);

	m_tcpClient.RequestHistory(
		static_cast<const char*>(usernameUtf8)
	);
}

void CMFCChatDlg::RefreshConversationList()
{
	if (!m_tcpClient.IsConnected())
		return;

	m_tcpClient.RequestConversations();
}

LRESULT CMFCChatDlg::OnTcpMessage(
	WPARAM wParam,
	LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);

	TcpMessage* incoming =
		reinterpret_cast<TcpMessage*>(lParam);

	if (incoming == nullptr)
		return 0;

	CString sender = CA2T(incoming->sender.c_str(), CP_UTF8);

	if (!incoming->isGroup)
	{
		RefreshConversationList();

		if (sender == m_currentChatUser)
		{
			m_tcpClient.MarkMessageRead(
				incoming->messageId,
				false
			);

			LoadChatHistory();
		}
	}

	delete incoming;

	return 0;
}

LRESULT CMFCChatDlg::OnTcpResponse(
	WPARAM wParam,
	LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);

	TcpResponse* response =
		reinterpret_cast<TcpResponse*>(lParam);

	if (response == nullptr)
		return 0;

	if (response->action == "CONVERSATIONS_RESULT")
	{
		DisplayConversationResponse(response->payload);
	}
	else if (response->action == "HISTORY_RESULT")
	{
		DisplayHistoryResponse(response->payload);
	}
	else if (response->action == "SEND_MESSAGE_RESULT")
	{
		bool saved = false;
		std::string errorText = "Unknown error";

		try
		{
			nlohmann::json result =
				nlohmann::json::parse(response->payload);

			saved = result.value("success", false);

			if (!saved)
			{
				errorText = result.value(
					"message", std::string("Unknown error"));
			}
		}
		catch (const nlohmann::json::exception&)
		{
			errorText = "Invalid response from server";
		}

		if (saved)
		{
			// Server đã lưu tin: tải lại khung chat để người gửi thấy
			// tin của chính mình, và cập nhật "Tin nhắn cuối" trong danh sách.
			LoadChatHistory();
			RefreshConversationList();
		}
		else
		{
			CA2T errorWide(errorText.c_str(), CP_UTF8);

			CString errorMessage;
			errorMessage.Format(
				L"Gửi tin nhắn thất bại: %s",
				static_cast<LPCTSTR>(errorWide));

			AfxMessageBox(errorMessage);
		}
	}

	delete response;

	return 0;
}

void CMFCChatDlg::DisplayConversationResponse(
	const std::string& payload)
{
	using json = nlohmann::json;

	json response;

	try
	{
		response = json::parse(payload);
	}
	catch (const json::exception&)
	{
		return;
	}

	if (!response.value("success", false) ||
		!response.contains("conversations") ||
		!response["conversations"].is_array())
	{
		return;
	}

	CString previousSelection = m_currentChatUser;

	m_conversationList.DeleteAllItems();
	m_fakeChats.RemoveAll();

	int selectedIndex = -1;

	for (const auto& item : response["conversations"])
	{
		std::string username =
			item.value("username", std::string());

		std::string lastMessage =
			item.value("lastMessage", std::string());

		std::string lastSentAt =
			item.value("lastSentAt", std::string());

		CA2T usernameText(username.c_str(), CP_UTF8);
		CA2T messageText(lastMessage.c_str(), CP_UTF8);
		CA2T timeText(lastSentAt.c_str(), CP_UTF8);

		CString preview(static_cast<LPCTSTR>(messageText));

		preview.Replace(L"\r", L" ");
		preview.Replace(L"\n", L" ");

		if (preview.GetLength() > 35)
			preview = preview.Left(32) + L"...";

		int index = m_conversationList.InsertItem(
			m_conversationList.GetItemCount(),
			static_cast<LPCTSTR>(usernameText)
		);

		m_conversationList.SetItemText(index, 1, preview);

		m_conversationList.SetItemText(
			index,
			2,
			static_cast<LPCTSTR>(timeText)
		);

		m_fakeChats[static_cast<LPCTSTR>(usernameText)] = preview;

		if (usernameText == previousSelection)
			selectedIndex = index;
	}

	if (selectedIndex < 0 &&
		m_conversationList.GetItemCount() > 0)
	{
		selectedIndex = 0;
	}

	if (selectedIndex >= 0)
	{
		m_conversationList.SetItemState(
			selectedIndex,
			LVIS_SELECTED | LVIS_FOCUSED,
			LVIS_SELECTED | LVIS_FOCUSED
		);

		m_conversationList.EnsureVisible(selectedIndex, FALSE);

		m_currentChatUser =
			m_conversationList.GetItemText(selectedIndex, 0);

		SetDlgItemText(IDC_STATIC_CHAT_USER, m_currentChatUser);

		LoadChatHistory();
	}
}

namespace
{
	// Kiểu hiển thị cho một đoạn (paragraph) trong RichEdit.
	struct RichStyle
	{
		COLORREF textColor = RGB(0, 0, 0);
		COLORREF backColor = CLR_INVALID;  // CLR_INVALID = không tô nền
		int      sizePt = 10;
		bool     bold = false;
		WORD     alignment = PFA_LEFT;     // PFA_LEFT / PFA_RIGHT / PFA_CENTER
		LONG     leftIndent = 0;            // đơn vị twips (1/1440 inch)
		LONG     rightIndent = 0;
		LONG     spaceAfter = 0;
	};

	// Thêm một đoạn text vào cuối RichEdit rồi áp dụng định dạng cho đúng đoạn đó.
	void AppendParagraph(
		CRichEditCtrl& edit,
		const CString& text,
		const RichStyle& style)
	{
		edit.SetSel(-1, -1);

		long start = 0;
		long unused = 0;
		edit.GetSel(start, unused);

		// RichEdit 2.0 dùng '\r' làm dấu ngắt đoạn.
		edit.ReplaceSel(text + _T("\r"));

		long end = 0;
		edit.GetSel(unused, end);

		if (end <= start)
			return;

		// Chọn phần chữ (bỏ dấu ngắt đoạn ở cuối) để áp dụng định dạng.
		edit.SetSel(start, end - 1);

		CHARFORMAT2 cf = {};
		cf.cbSize = sizeof(CHARFORMAT2);
		cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_SIZE |
			CFM_FACE | CFM_BACKCOLOR;
		cf.dwEffects = style.bold ? CFE_BOLD : 0;
		cf.yHeight = style.sizePt * 20;
		cf.crTextColor = style.textColor;
		_tcscpy_s(cf.szFaceName, _T("Segoe UI"));

		if (style.backColor == CLR_INVALID)
		{
			cf.dwEffects |= CFE_AUTOBACKCOLOR;
		}
		else
		{
			cf.crBackColor = style.backColor;
		}

		edit.SendMessage(
			EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

		PARAFORMAT2 pf = {};
		pf.cbSize = sizeof(PARAFORMAT2);
		pf.dwMask = PFM_ALIGNMENT | PFM_STARTINDENT |
			PFM_RIGHTINDENT | PFM_SPACEAFTER;
		pf.wAlignment = style.alignment;
		pf.dxStartIndent = style.leftIndent;
		pf.dxRightIndent = style.rightIndent;
		pf.dySpaceAfter = style.spaceAfter;

		edit.SendMessage(
			EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));

		edit.SetSel(-1, -1);
	}

	// Server lưu "YYYY-MM-DD HH:MM:SS" theo UTC -> đổi sang giờ của máy.
	bool ParseUtcToLocal(const std::string& sentAt, SYSTEMTIME& local)
	{
		int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

		if (sscanf_s(
			sentAt.c_str(),
			"%d-%d-%d %d:%d:%d",
			&y, &mo, &d, &h, &mi, &s) != 6)
		{
			return false;
		}

		SYSTEMTIME utc = {};
		utc.wYear = static_cast<WORD>(y);
		utc.wMonth = static_cast<WORD>(mo);
		utc.wDay = static_cast<WORD>(d);
		utc.wHour = static_cast<WORD>(h);
		utc.wMinute = static_cast<WORD>(mi);
		utc.wSecond = static_cast<WORD>(s);

		return SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) != FALSE;
	}
}

void CMFCChatDlg::DisplayHistoryResponse(
	const std::string& payload)
{
	using json = nlohmann::json;

	json response;

	try
	{
		response = json::parse(payload);
	}
	catch (const json::exception&)
	{
		return;
	}

	if (!response.value("success", false) ||
		!response.contains("messages") ||
		!response["messages"].is_array())
	{
		return;
	}

	std::string otherUsername =
		response.value("otherUsername", std::string());

	CT2A currentChatUtf8(m_currentChatUser, CP_UTF8);

	if (otherUsername !=
		static_cast<const char*>(currentChatUtf8))
	{
		return;
	}

	CRichEditCtrl* pEdit =
		static_cast<CRichEditCtrl*>(GetDlgItem(IDC_RICHEDIT_MESSAGE));

	if (pEdit == nullptr)
		return;

	CRichEditCtrl& edit = *pEdit;

	m_messageHistory.Empty();

	edit.SetRedraw(FALSE);
	edit.SetReadOnly(TRUE);
	edit.SetBackgroundColor(FALSE, RGB(250, 250, 252));
	edit.SetWindowText(_T(""));

	// ---- Các kiểu hiển thị ----
	RichStyle dateStyle;
	dateStyle.textColor = RGB(140, 140, 140);
	dateStyle.sizePt = 8;
	dateStyle.alignment = PFA_CENTER;
	dateStyle.spaceAfter = 120;

	RichStyle headerStyle;               // "tên + giờ"
	headerStyle.textColor = RGB(37, 150, 190);
	headerStyle.sizePt = 8;
	headerStyle.bold = true;

	RichStyle myBody;                    // tin của mình: bên phải
	myBody.textColor = RGB(13, 12, 8);
	myBody.sizePt = 11;
	myBody.alignment = PFA_RIGHT;
	myBody.leftIndent = 1800;
	myBody.rightIndent = 100;
	myBody.spaceAfter = 160;

	RichStyle otherBody;                 // tin người khác: bên trái
	otherBody.textColor = RGB(13, 12, 8);
	otherBody.sizePt = 11;
	otherBody.alignment = PFA_LEFT;
	otherBody.leftIndent = 100;
	otherBody.rightIndent = 1800;
	otherBody.spaceAfter = 160;

	const auto& messages = response["messages"];

	if (messages.empty())
	{
		AppendParagraph(
			edit,
			_T("Chưa có tin nhắn. Hãy bắt đầu cuộc trò chuyện!"),
			dateStyle);
	}

	int lastYear = -1, lastMonth = -1, lastDay = -1;

	for (const auto& item : messages)
	{
		std::string sender =
			item.value("sender", std::string());

		std::string content =
			item.value("content", std::string());

		std::string sentAt =
			item.value("sentAt", std::string());

		CA2T senderText(sender.c_str(), CP_UTF8);
		CA2T contentText(content.c_str(), CP_UTF8);
		CA2T timeText(sentAt.c_str(), CP_UTF8);

		const bool isMine =
			m_currentUser.CompareNoCase(
				static_cast<LPCTSTR>(senderText)) == 0;

		SYSTEMTIME local = {};
		const bool hasTime = ParseUtcToLocal(sentAt, local);

		// Dòng phân cách khi sang ngày mới.
		if (hasTime &&
			(local.wYear != lastYear ||
				local.wMonth != lastMonth ||
				local.wDay != lastDay))
		{
			CString dateLabel;
			dateLabel.Format(
				_T("—  %02d/%02d/%04d  —"),
				local.wDay, local.wMonth, local.wYear);

			AppendParagraph(edit, dateLabel, dateStyle);

			lastYear = local.wYear;
			lastMonth = local.wMonth;
			lastDay = local.wDay;
		}

		CString timeLabel;

		if (hasTime)
		{
			timeLabel.Format(_T("%02d:%02d"), local.wHour, local.wMinute);
		}
		else
		{
			timeLabel = static_cast<LPCTSTR>(timeText);
		}

		CString header;

		if (isMine)
		{
			header = timeLabel;
		}
		else
		{
			header.Format(
				_T("%s  %s"),
				static_cast<LPCTSTR>(senderText),
				static_cast<LPCTSTR>(timeLabel));
		}

		RichStyle hs = headerStyle;
		hs.alignment = isMine ? PFA_RIGHT : PFA_LEFT;
		hs.leftIndent = isMine ? 1800 : 100;
		hs.rightIndent = isMine ? 100 : 1800;

		AppendParagraph(edit, header, hs);

		// Thêm khoảng trắng hai đầu để vùng nền trông giống bong bóng chat.
		CString body;
		body += static_cast<LPCTSTR>(contentText);

		AppendParagraph(edit, body, isMine ? myBody : otherBody);
	}

	edit.SetSel(-1, -1);
	edit.SendMessage(EM_SCROLLCARET, 0, 0);
	edit.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);

	edit.SetRedraw(TRUE);
	edit.Invalidate();
}