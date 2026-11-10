// MFCChatDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "MFCChat.h"
#include "MFCChatDlg.h"
#include "afxdialogex.h"
#include "CCreateGroupDlg.h"
#include <atlconv.h>
#include <vector>
#include "TcpChatClient.h"
#include <string>
#include "json.hpp"
#include <cstdio>
#include <uxtheme.h>

#pragma comment(lib, "uxtheme.lib")

#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <richole.h>
#include "Base64.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

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



namespace
{
	// Tiền tố hiển thị trước tên nhóm trong danh sách hội thoại.
	const TCHAR kGroupPrefix[] = _T("[Nhóm] ");

	CString FromUtf8(const std::string& text)
	{
		return CString(static_cast<LPCTSTR>(CA2T(text.c_str(), CP_UTF8)));
	}

	std::string ToUtf8(const CString& text)
	{
		CT2A converted(text, CP_UTF8);
		return std::string(static_cast<const char*>(converted));
	}

	// Tin nhắn có tệp lưu nội dung dạng "@@FILE@@" + JSON {id, kind, name, size}.
	const char kAttachmentPrefix[] = "@@FILE@@";

	bool ParseAttachmentContent(
		const std::string& content,
		ChatAttachmentLink& out)
	{
		const size_t prefixLength = sizeof(kAttachmentPrefix) - 1;

		if (content.compare(0, prefixLength, kAttachmentPrefix) != 0)
			return false;

		try
		{
			nlohmann::json meta =
				nlohmann::json::parse(content.substr(prefixLength));

			out.id = meta.value("id", int64_t(0));
			out.kind = meta.value("kind", std::string());
			out.name = meta.value("name", std::string());
			out.size = meta.value("size", int64_t(0));
			out.thumb = meta.value("thumb", false);
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}

		return out.id > 0;
	}

	CString FormatFileSize(int64_t bytes)
	{
		CString text;

		if (bytes >= 1024 * 1024)
			text.Format(_T("%.1f MB"), static_cast<double>(bytes) / (1024.0 * 1024.0));
		else if (bytes >= 1024)
			text.Format(_T("%.0f KB"), static_cast<double>(bytes) / 1024.0);
		else
			text.Format(_T("%d B"), static_cast<int>(bytes));

		return text;
	}

	CString MakePreview(const std::string& utf8Text)
	{
		// Tin có tệp: hiển thị loại tệp thay vì chuỗi JSON.
		ChatAttachmentLink attachment;

		if (ParseAttachmentContent(utf8Text, attachment))
			return attachment.kind == "video" ? _T("[Video]") : _T("[Ảnh]");

		CString preview = FromUtf8(utf8Text);

		preview.Replace(_T("\r"), _T(" "));
		preview.Replace(_T("\n"), _T(" "));

		if (preview.GetLength() > 35)
			preview = preview.Left(32) + _T("...");

		return preview;
	}
}

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
	ON_BN_CLICKED(IDC_BUTTON_CREATE_GROUP, &CMFCChatDlg::OnBnClickedCreateGroup)
	ON_WM_DRAWITEM()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_BN_CLICKED(IDC_BUTTON_ATTACH, &CMFCChatDlg::OnBnClickedAttach)
	ON_BN_CLICKED(IDC_BUTTON_MY_AVATAR, &CMFCChatDlg::OnBnClickedMyAvatar)
	ON_WM_DROPFILES()
	ON_WM_DESTROY()
	ON_NOTIFY(EN_LINK, IDC_RICHEDIT_MESSAGE, &CMFCChatDlg::OnRichEditLink)
	ON_NOTIFY(EN_MSGFILTER, IDC_RICHEDIT_MESSAGE, &CMFCChatDlg::OnRichEditMsgFilter)
	ON_MESSAGE(WM_APP_UPLOAD_PROGRESS, &CMFCChatDlg::OnUploadProgress)
	ON_WM_CTLCOLOR()
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_CONVERSATION, &CMFCChatDlg::OnCustomDrawConversationList)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CONVERSATION, &CMFCChatDlg::OnLvnItemchangedListConversation)
	ON_MESSAGE(WM_APP_TCP_MESSAGE, &CMFCChatDlg::OnTcpMessage)
	ON_MESSAGE(WM_APP_TCP_RESPONSE, &CMFCChatDlg::OnTcpResponse)
END_MESSAGE_MAP()

// CMFCChatDlg message handlers

BOOL CMFCChatDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// GDI+ dùng để vẽ avatar tròn và thu nhỏ ảnh đại diện.
	Gdiplus::GdiplusStartupInput gdiplusInput;
	Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusInput, nullptr);

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

	// Nút "Tạo nhóm": tạo bằng code, đặt ngay dưới danh sách hội thoại.
	{
		CRect listRect;
		m_conversationList.GetWindowRect(&listRect);
		ScreenToClient(&listRect);

		const int buttonHeight = 26;
		const int gap = 4;

		CRect buttonRect(
			listRect.left,
			listRect.bottom - buttonHeight,
			listRect.right,
			listRect.bottom);

		listRect.bottom -= (buttonHeight + gap);
		m_conversationList.MoveWindow(&listRect);

		m_btnCreateGroup.Create(
			L"+ Tạo nhóm",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			buttonRect,
			this,
			IDC_BUTTON_CREATE_GROUP);

		m_btnCreateGroup.SetFont(GetFont());
	}

	ModifyStyle(0, WS_CLIPCHILDREN);   // giảm nhấp nháy khi co giãn

	ApplyChatTheme();
	ApplyMediaUi();
	RegisterAnchors();

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

		// Tải ảnh đại diện của chính mình.
		RequestAvatarIfNeeded(ToUtf8(m_currentUser), -1);
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

	const bool sent = m_isGroupChat
		? m_tcpClient.SendGroupMessage(m_currentGroupId, content)
		: m_tcpClient.SendMessageTo(receiver, content);

	if (!sent)
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

	// Khi đang dựng lại danh sách thì bỏ qua; RebuildConversationList tự xử lý.
	if (!m_rebuildingList &&
		(pNMLV->uChanged & LVIF_STATE) &&
		(pNMLV->uNewState & LVIS_SELECTED) &&
		!(pNMLV->uOldState & LVIS_SELECTED))
	{
		int index = pNMLV->iItem;

		if (index >= 0)
		{
			ApplySelection(index);

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

	if (m_isGroupChat)
	{
		if (m_currentGroupId > 0)
			m_tcpClient.RequestGroupHistory(m_currentGroupId);

		return;
	}

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
	m_tcpClient.RequestGroups();
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

		if (!m_isGroupChat && sender == m_currentChatUser)
		{
			m_tcpClient.MarkMessageRead(
				incoming->messageId,
				false
			);

			LoadChatHistory();
		}
	}
	else
	{
		// Tin nhắn nhóm: làm mới danh sách (có thể là nhóm mới hoặc tin cuối đổi).
		RefreshConversationList();

		if (m_isGroupChat && incoming->groupId == m_currentGroupId)
		{
			m_tcpClient.MarkMessageRead(
				incoming->messageId,
				true
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
	else if (response->action == "THUMBNAIL_RESULT")
	{
		OnThumbnailResult(response->payload);
	}
	else if (response->action == "UPLOAD_READY")
	{
		OnUploadReady(response->payload);
	}
	else if (response->action == "UPLOAD_RESULT")
	{
		OnUploadResult(response->payload);
	}
	else if (response->action == "ATTACHMENT_CHUNK")
	{
		OnAttachmentChunk(response->payload);
	}
	else if (response->action == "AVATAR_RESULT")
	{
		OnAvatarResult(response->payload);
	}
	else if (response->action == "SET_AVATAR_RESULT")
	{
		OnSetAvatarResult(response->payload);
	}
	else if (response->action == "GROUPS_RESULT")
	{
		DisplayGroupsResponse(response->payload);
	}
	else if (response->action == "GROUP_HISTORY_RESULT")
	{
		DisplayGroupHistoryResponse(response->payload);
	}
	else if (response->action == "CREATE_GROUP_RESULT")
	{
		bool created = false;
		std::string errorText = "Unknown error";

		try
		{
			nlohmann::json result =
				nlohmann::json::parse(response->payload);

			created = result.value("success", false);

			if (created)
			{
				// Chọn luôn nhóm vừa tạo sau khi danh sách được làm mới.
				std::string groupName =
					result.value("groupName", std::string());

				m_isGroupChat = true;
				m_currentGroupId = result.value("groupId", int64_t(0));
				m_currentChatUser = FromUtf8(groupName);
			}
			else
			{
				errorText = result.value(
					"message", std::string("Unknown error"));
			}
		}
		catch (const nlohmann::json::exception&)
		{
			errorText = "Invalid response from server";
		}

		if (created)
		{
			// Chỉ cần tải lại danh sách nhóm; nhóm mới đã được đặt làm
			// hội thoại hiện tại nên sẽ được chọn khi danh sách về.
			m_tcpClient.RequestGroups();
		}
		else
		{
			CString errorMessage;
			errorMessage.Format(
				L"Tạo nhóm thất bại: %s",
				static_cast<LPCTSTR>(FromUtf8(errorText)));

			AfxMessageBox(errorMessage);
		}
	}
	else if (response->action == "SEND_MESSAGE_RESULT" ||
		response->action == "SEND_GROUP_MESSAGE_RESULT")
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

	m_userItems.clear();

	for (const auto& item : response["conversations"])
	{
		ConversationItem row;

		row.name = FromUtf8(item.value("username", std::string()));
		row.preview = MakePreview(item.value("lastMessage", std::string()));
		row.time = FromUtf8(item.value("lastSentAt", std::string()));
		row.avatarVersion = item.value("avatarVersion", 0);

		m_userItems.push_back(row);
	}

	RebuildConversationList();

	// Tải avatar mới/đã đổi của những người trong danh sách.
	for (const auto& user : m_userItems)
		RequestAvatarIfNeeded(ToUtf8(user.name), user.avatarVersion);
}

// ============ Chèn ảnh vào RichEdit bằng đối tượng OLE ============
namespace
{
	// IDataObject tối giản chứa một HBITMAP (CF_BITMAP) để OleCreateFromData
	// tạo ra đối tượng ảnh nhúng.
	class CBitmapDataObject : public IDataObject
	{
	public:
		explicit CBitmapDataObject(HBITMAP bitmap) : m_bitmap(bitmap) {}

		STDMETHOD(QueryInterface)(REFIID iid, void** object) override
		{
			if (object == nullptr)
				return E_POINTER;

			if (iid == IID_IUnknown || iid == IID_IDataObject)
			{
				*object = static_cast<IDataObject*>(this);
				AddRef();
				return S_OK;
			}

			*object = nullptr;
			return E_NOINTERFACE;
		}

		STDMETHOD_(ULONG, AddRef)() override
		{
			return static_cast<ULONG>(::InterlockedIncrement(&m_refCount));
		}

		STDMETHOD_(ULONG, Release)() override
		{
			const LONG count = ::InterlockedDecrement(&m_refCount);

			if (count == 0)
				delete this;

			return static_cast<ULONG>(count);
		}

		STDMETHOD(GetData)(FORMATETC* format, STGMEDIUM* medium) override
		{
			if (format == nullptr || medium == nullptr)
				return E_INVALIDARG;

			if (format->cfFormat != CF_BITMAP || !(format->tymed & TYMED_GDI))
				return DV_E_FORMATETC;

			medium->tymed = TYMED_GDI;
			medium->hBitmap = static_cast<HBITMAP>(
				::OleDuplicateData(m_bitmap, CF_BITMAP, 0));
			medium->pUnkForRelease = nullptr;

			return medium->hBitmap != nullptr ? S_OK : E_OUTOFMEMORY;
		}

		STDMETHOD(GetDataHere)(FORMATETC*, STGMEDIUM*) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(QueryGetData)(FORMATETC* format) override
		{
			if (format != nullptr &&
				format->cfFormat == CF_BITMAP &&
				(format->tymed & TYMED_GDI))
			{
				return S_OK;
			}

			return DV_E_FORMATETC;
		}

		STDMETHOD(GetCanonicalFormatEtc)(FORMATETC*, FORMATETC* out) override
		{
			if (out != nullptr)
				out->ptd = nullptr;

			return E_NOTIMPL;
		}

		STDMETHOD(SetData)(FORMATETC*, STGMEDIUM*, BOOL) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(EnumFormatEtc)(DWORD, IEnumFORMATETC**) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

		STDMETHOD(DUnadvise)(DWORD) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

		STDMETHOD(EnumDAdvise)(IEnumSTATDATA**) override
		{
			return OLE_E_ADVISENOTSUPPORTED;
		}

	private:
		LONG m_refCount = 1;
		HBITMAP m_bitmap;   // do bên ngoài sở hữu, không xóa ở đây
	};

	// {00020D03-0000-0000-C000-000000000046}
	// Tự khai báo để không phụ thuộc vào việc thư viện nào chứa IID này.
	const IID kIidRichEditOleCallback =
	{ 0x00020d03, 0x0000, 0x0000,
	  { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

	// RichEdit chỉ cho phép chèn đối tượng OLE khi có callback này.
	class CRichEditOleCallback : public IRichEditOleCallback
	{
	public:
		STDMETHOD(QueryInterface)(REFIID iid, void** object) override
		{
			if (object == nullptr)
				return E_POINTER;

			if (iid == IID_IUnknown || iid == kIidRichEditOleCallback)
			{
				*object = static_cast<IRichEditOleCallback*>(this);
				AddRef();
				return S_OK;
			}

			*object = nullptr;
			return E_NOINTERFACE;
		}

		STDMETHOD_(ULONG, AddRef)() override
		{
			return static_cast<ULONG>(::InterlockedIncrement(&m_refCount));
		}

		STDMETHOD_(ULONG, Release)() override
		{
			const LONG count = ::InterlockedDecrement(&m_refCount);

			if (count == 0)
				delete this;

			return static_cast<ULONG>(count);
		}

		STDMETHOD(GetNewStorage)(LPSTORAGE* storage) override
		{
			if (storage == nullptr)
				return E_INVALIDARG;

			*storage = nullptr;

			ILockBytes* lockBytes = nullptr;
			HRESULT hr = ::CreateILockBytesOnHGlobal(nullptr, TRUE, &lockBytes);

			if (FAILED(hr))
				return hr;

			hr = ::StgCreateDocfileOnILockBytes(
				lockBytes,
				STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE,
				0, storage);

			lockBytes->Release();
			return hr;
		}

		STDMETHOD(GetInPlaceContext)(
			LPOLEINPLACEFRAME*, LPOLEINPLACEUIWINDOW*,
			LPOLEINPLACEFRAMEINFO) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(ShowContainerUI)(BOOL) override { return E_NOTIMPL; }

		STDMETHOD(QueryInsertObject)(LPCLSID, LPSTORAGE, LONG) override
		{
			return S_OK;
		}

		STDMETHOD(DeleteObject)(LPOLEOBJECT) override { return S_OK; }

		STDMETHOD(QueryAcceptData)(
			LPDATAOBJECT, CLIPFORMAT*, DWORD, BOOL, HGLOBAL) override
		{
			return S_OK;
		}

		STDMETHOD(ContextSensitiveHelp)(BOOL) override { return S_OK; }

		STDMETHOD(GetClipboardData)(
			CHARRANGE*, DWORD, LPDATAOBJECT*) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(GetDragDropEffect)(BOOL, DWORD, LPDWORD) override
		{
			return E_NOTIMPL;
		}

		STDMETHOD(GetContextMenu)(
			WORD, LPOLEOBJECT, CHARRANGE*, HMENU*) override
		{
			return E_NOTIMPL;
		}

	private:
		LONG m_refCount = 1;
	};

	// Ghi HRESULT ra cửa sổ Output của Visual Studio để chẩn đoán khi chèn ảnh lỗi.
	void TraceHr(const wchar_t* what, HRESULT hr)
	{
		CString message;
		message.Format(
			L"[MFCChat] %s -> 0x%08X\n", what, static_cast<unsigned>(hr));

		::OutputDebugString(message);
	}

	// Chèn một bitmap thành đối tượng nhúng tại vị trí con trỏ hiện tại.
	// Thử đối tượng ảnh TĨNH trước (không cần ứng dụng OLE như Paint, nên chạy
	// được trên Windows 11), nếu lỗi mới thử đối tượng thường.
	bool InsertBitmapObject(IRichEditOle* richEditOle, HBITMAP bitmap)
	{
		if (richEditOle == nullptr || bitmap == nullptr)
			return false;

		IOleClientSite* clientSite = nullptr;

		HRESULT hr = richEditOle->GetClientSite(&clientSite);

		if (FAILED(hr) || clientSite == nullptr)
		{
			TraceHr(L"GetClientSite", FAILED(hr) ? hr : E_FAIL);
			return false;
		}

		CBitmapDataObject* dataObject = new CBitmapDataObject(bitmap);
		bool inserted = false;

		for (int attempt = 0; attempt < 2 && !inserted; ++attempt)
		{
			const bool useStatic = (attempt == 0);

			ILockBytes* lockBytes = nullptr;
			IStorage* storage = nullptr;
			IOleObject* object = nullptr;

			hr = ::CreateILockBytesOnHGlobal(nullptr, TRUE, &lockBytes);

			if (SUCCEEDED(hr))
			{
				hr = ::StgCreateDocfileOnILockBytes(
					lockBytes,
					STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE,
					0, &storage);
			}

			if (SUCCEEDED(hr))
			{
				FORMATETC format = {};
				format.cfFormat = CF_BITMAP;
				format.ptd = nullptr;
				format.dwAspect = DVASPECT_CONTENT;
				format.lindex = -1;
				format.tymed = TYMED_GDI;

				if (useStatic)
				{
					hr = ::OleCreateStaticFromData(
						dataObject, IID_IOleObject, OLERENDER_FORMAT, &format,
						clientSite, storage,
						reinterpret_cast<void**>(&object));
				}
				else
				{
					hr = ::OleCreateFromData(
						dataObject, IID_IOleObject, OLERENDER_FORMAT, &format,
						clientSite, storage,
						reinterpret_cast<void**>(&object));
				}

				TraceHr(
					useStatic ? L"OleCreateStaticFromData" : L"OleCreateFromData",
					hr);

				if (SUCCEEDED(hr) && object != nullptr)
				{
					::OleSetContainedObject(object, TRUE);

					REOBJECT reObject = {};
					reObject.cbStruct = sizeof(REOBJECT);

					CLSID clsid = {};
					object->GetUserClassID(&clsid);

					reObject.clsid = clsid;
					reObject.cp = REO_CP_SELECTION;
					reObject.dvaspect = DVASPECT_CONTENT;
					reObject.dwFlags = REO_RESIZABLE | REO_BELOWBASELINE |
						(useStatic ? REO_STATIC : 0);
					reObject.poleobj = object;
					reObject.polesite = clientSite;
					reObject.pstg = storage;
					reObject.sizel.cx = 0;
					reObject.sizel.cy = 0;

					hr = richEditOle->InsertObject(&reObject);
					TraceHr(L"InsertObject", hr);

					inserted = SUCCEEDED(hr);
				}
			}
			else
			{
				TraceHr(L"CreateStorage", hr);
			}

			if (object != nullptr)
				object->Release();

			if (storage != nullptr)
				storage->Release();

			if (lockBytes != nullptr)
				lockBytes->Release();
		}

		dataObject->Release();
		clientSite->Release();

		return inserted;
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
		bool     link = false;        // hiển thị như liên kết bấm được
	};

	// Thêm một đoạn text vào cuối RichEdit rồi áp dụng định dạng cho đúng đoạn đó.
	long AppendParagraph(
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
			return -1;

		// Chọn phần chữ (bỏ dấu ngắt đoạn ở cuối) để áp dụng định dạng.
		edit.SetSel(start, end - 1);

		CHARFORMAT2 cf = {};
		cf.cbSize = sizeof(CHARFORMAT2);
		cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_SIZE |
			CFM_FACE | CFM_BACKCOLOR | CFM_LINK | CFM_UNDERLINE;
		cf.dwEffects = (style.bold ? CFE_BOLD : 0) |
			(style.link ? (CFE_LINK | CFE_UNDERLINE) : 0);
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

		return start;
	}

	struct RtfCookie
	{
		const std::string* data;
		size_t position;
	};

	DWORD CALLBACK RtfStreamIn(
		DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* done)
	{
		RtfCookie* source = reinterpret_cast<RtfCookie*>(cookie);

		const size_t remaining = source->data->size() - source->position;
		const size_t chunk =
			remaining < static_cast<size_t>(count)
			? remaining
			: static_cast<size_t>(count);

		memcpy(buffer, source->data->data() + source->position, chunk);

		source->position += chunk;
		*done = static_cast<LONG>(chunk);

		return 0;
	}

	// Đường dự phòng: chèn ảnh JPEG bằng RTF (\pict\jpegblip) tại vị trí con trỏ.
	bool InsertJpegViaRtf(
		CRichEditCtrl& edit,
		const std::string& jpeg,
		int width,
		int height)
	{
		static const char digits[] = "0123456789abcdef";

		std::string rtf =
			"{\\rtf1\\ansi{\\pict\\jpegblip\\picw" + std::to_string(width) +
			"\\pich" + std::to_string(height) +
			"\\picwgoal" + std::to_string(width * 15) +      // 1 px = 15 twips
			"\\pichgoal" + std::to_string(height * 15) + " ";

		rtf.reserve(rtf.size() + jpeg.size() * 2 + 4);

		for (unsigned char c : jpeg)
		{
			rtf.push_back(digits[c >> 4]);
			rtf.push_back(digits[c & 0x0F]);
		}

		rtf += "}}";

		RtfCookie cookie = { &rtf, 0 };

		EDITSTREAM stream = {};
		stream.dwCookie = reinterpret_cast<DWORD_PTR>(&cookie);
		stream.pfnCallback = RtfStreamIn;

		edit.StreamIn(SF_RTF | SFF_SELECTION, stream);

		return stream.dwError == 0;
	}

	// Chèn một ảnh thành một đoạn riêng.
	// Trả về vị trí ký tự của ảnh, hoặc -1 nếu chèn không được.
	long AppendImageParagraph(
		CRichEditCtrl& edit,
		const ChatThumbnail& thumbnail,
		const RichStyle& style)
	{
		if (thumbnail.bitmap == nullptr)
			return -1;

		edit.SetSel(-1, -1);

		long start = 0;
		long unused = 0;
		edit.GetSel(start, unused);

		IRichEditOle* richEditOle = edit.GetIRichEditOle();

		if (richEditOle == nullptr)
		{
			TraceHr(L"GetIRichEditOle", E_FAIL);
			return -1;
		}

		// Ảnh chèn thành công thì số ký tự (và số đối tượng) tăng lên.
		const long lengthBefore = edit.GetTextLength();
		const LONG objectsBefore = richEditOle->GetObjectCount();

		auto wasInserted = [&]() -> bool
		{
			return edit.GetTextLength() > lengthBefore ||
				richEditOle->GetObjectCount() > objectsBefore;
		};

		// Cách 1: đối tượng OLE (ảnh tĩnh).
		bool inserted =
			InsertBitmapObject(richEditOle, thumbnail.bitmap) && wasInserted();

		// Cách 2 (dự phòng): chèn JPEG qua RTF.
		if (!inserted && !thumbnail.jpeg.empty())
		{
			edit.SetSel(-1, -1);

			InsertJpegViaRtf(
				edit, thumbnail.jpeg, thumbnail.width, thumbnail.height);

			inserted = wasInserted();
			TraceHr(L"RTF jpegblip", inserted ? S_OK : E_FAIL);
		}

		richEditOle->Release();

		if (!inserted)
			return -1;

		edit.SetSel(-1, -1);
		edit.ReplaceSel(_T("\r"));

		long end = 0;
		edit.GetSel(unused, end);

		if (end <= start)
			return -1;

		// Chọn đúng ký tự chứa ảnh để áp dụng căn lề/thụt lề cho đoạn.
		edit.SetSel(start, end - 1);

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

		return start;
	}

	// Đổi màu chữ cho đoạn [start, end) mà không đụng tới các định dạng khác.
	void ColorRange(
		CRichEditCtrl& edit,
		long start,
		long end,
		COLORREF color)
	{
		if (start < 0 || end <= start)
			return;

		edit.SetSel(start, end);

		CHARFORMAT2 cf = {};
		cf.cbSize = sizeof(CHARFORMAT2);
		cf.dwMask = CFM_COLOR;
		cf.crTextColor = color;

		edit.SendMessage(
			EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

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

namespace
{
	// Vẽ danh sách tin nhắn (chat 1-1 hoặc nhóm) vào RichEdit.
	void RenderMessages(
		CRichEditCtrl& edit,
		const nlohmann::json& messages,
		const CString& currentUser,
		std::vector<ChatAttachmentLink>* links,
		const std::map<int64_t, ChatThumbnail>* thumbs,
		std::set<int64_t>* missingThumbs)
	{
		edit.SetRedraw(FALSE);
		// Chèn ảnh/đối tượng có thể bị từ chối khi RichEdit ở chế độ chỉ đọc,
		// nên mở khóa trong lúc dựng nội dung rồi khóa lại ở cuối.
		edit.SetReadOnly(FALSE);
		edit.SetBackgroundColor(FALSE, RGB(250, 250, 252));
		edit.SetWindowText(_T(""));

		// ---- Các kiểu hiển thị ----
		RichStyle dateStyle;
		dateStyle.textColor = RGB(140, 140, 140);
		dateStyle.sizePt = 8;
		dateStyle.alignment = PFA_CENTER;
		dateStyle.spaceAfter = 120;

		RichStyle headerStyle;               // "tên  giờ"
		headerStyle.textColor = RGB(120, 120, 120);
		headerStyle.sizePt = 8;
		headerStyle.bold = true;

		RichStyle myBody;                    // tin của mình: căn phải
		myBody.textColor = RGB(30, 30, 30);
		myBody.sizePt = 11;
		myBody.alignment = PFA_RIGHT;
		myBody.leftIndent = 1800;
		myBody.rightIndent = 100;
		myBody.spaceAfter = 160;

		RichStyle otherBody;                 // tin người khác: căn trái
		otherBody.textColor = RGB(30, 30, 30);
		otherBody.sizePt = 11;
		otherBody.alignment = PFA_LEFT;
		otherBody.leftIndent = 100;
		otherBody.rightIndent = 1800;
		otherBody.spaceAfter = 160;

		// Màu tên: người khác = đỏ, mình = xanh dương.
		const COLORREF otherNameColor = RGB(220, 0, 0);
		const COLORREF myNameColor = RGB(0, 0, 205);

		if (messages.empty())
		{
			AppendParagraph(
				edit,
				_T("Chưa có tin nhắn. Hãy bắt đầu cuộc trò chuyện!"),
				dateStyle);
		}

		// ---- Bước 1: đọc dữ liệu và quyết định tin nào bắt đầu một nhóm ----
		// Cùng người gửi, cùng ngày, cách nhau tối đa 5 phút => gộp, chỉ hiện
		// tên + giờ ở tin đầu tiên.
		struct Entry
		{
			std::string sender;
			std::string content;
			std::string sentAt;
			bool isMine = false;
			bool hasTime = false;
			SYSTEMTIME local = {};
			bool showHeader = true;
			bool groupedWithNext = false;
		};

		std::vector<Entry> entries;
		entries.reserve(messages.size());

		for (const auto& item : messages)
		{
			Entry entry;

			entry.sender = item.value("sender", std::string());
			entry.content = item.value("content", std::string());
			entry.sentAt = item.value("sentAt", std::string());

			CA2T senderText(entry.sender.c_str(), CP_UTF8);

			entry.isMine =
				currentUser.CompareNoCase(
					static_cast<LPCTSTR>(senderText)) == 0;

			entry.hasTime = ParseUtcToLocal(entry.sentAt, entry.local);

			entries.push_back(std::move(entry));
		}

		auto toTicks = [](const SYSTEMTIME& st) -> ULONGLONG
		{
			FILETIME ft = {};
			SystemTimeToFileTime(&st, &ft);

			ULARGE_INTEGER value;
			value.LowPart = ft.dwLowDateTime;
			value.HighPart = ft.dwHighDateTime;

			return value.QuadPart;
		};

		const ULONGLONG groupWindow = 5ULL * 60ULL * 10000000ULL;   // 5 phút

		for (size_t i = 1; i < entries.size(); ++i)
		{
			const Entry& prev = entries[i - 1];
			Entry& cur = entries[i];

			if (!prev.hasTime || !cur.hasTime || prev.sender != cur.sender)
				continue;

			if (prev.local.wYear != cur.local.wYear ||
				prev.local.wMonth != cur.local.wMonth ||
				prev.local.wDay != cur.local.wDay)
			{
				continue;
			}

			const ULONGLONG a = toTicks(prev.local);
			const ULONGLONG b = toTicks(cur.local);

			if (b >= a && (b - a) <= groupWindow)
				cur.showHeader = false;
		}

		for (size_t i = 0; i + 1 < entries.size(); ++i)
			entries[i].groupedWithNext = !entries[i + 1].showHeader;

		// ---- Bước 2: vẽ ----
		int lastYear = -1, lastMonth = -1, lastDay = -1;

		for (const Entry& entry : entries)
		{
			CA2T senderText(entry.sender.c_str(), CP_UTF8);
			CA2T contentText(entry.content.c_str(), CP_UTF8);
			CA2T timeText(entry.sentAt.c_str(), CP_UTF8);

			const bool isMine = entry.isMine;
			const SYSTEMTIME& local = entry.local;

			// Dòng phân cách khi sang ngày mới.
			if (entry.hasTime &&
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

			if (entry.showHeader)
			{
				CString timeLabel;

				if (entry.hasTime)
				{
					timeLabel.Format(
						_T("%02d:%02d"), local.wHour, local.wMinute);
				}
				else
				{
					timeLabel = static_cast<LPCTSTR>(timeText);
				}

				CString header;

				if (isMine)
				{
					header = timeLabel;   // tin của mình: chỉ hiện giờ
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

				const long headerStart = AppendParagraph(edit, header, hs);

				// Chỉ tô màu phần tên; phần giờ giữ màu xám.
				// Với tin của mình, dòng này chỉ có giờ nên tô cả dòng.
				const int nameLength = isMine
					? header.GetLength()
					: CString(static_cast<LPCTSTR>(senderText)).GetLength();

				ColorRange(
					edit,
					headerStart,
					headerStart + nameLength,
					isMine ? myNameColor : otherNameColor);
			}

			// Tin trong cùng một nhóm sát nhau hơn; tin cuối nhóm cách xa hơn.
			RichStyle bodyStyle = isMine ? myBody : otherBody;
			bodyStyle.spaceAfter = entry.groupedWithNext ? 40 : 160;

			ChatAttachmentLink attachment;

			if (ParseAttachmentContent(entry.content, attachment))
			{
				bool imageShown = false;

				// Ảnh có bản thu nhỏ: hiển thị ngay trong khung chat.
				if (attachment.kind == "image" && attachment.thumb)
				{
					const ChatThumbnail* thumbnail = nullptr;

					if (thumbs != nullptr)
					{
						auto found = thumbs->find(attachment.id);

						if (found != thumbs->end())
							thumbnail = &found->second;
					}

					if (thumbnail != nullptr && thumbnail->bitmap != nullptr)
					{
						RichStyle imageStyle = bodyStyle;
						imageStyle.spaceAfter = 20;

						const long imagePos =
							AppendImageParagraph(edit, *thumbnail, imageStyle);

						if (imagePos >= 0)
						{
							imageShown = true;
							attachment.imagePos = imagePos;
							attachment.imageWidth = thumbnail->width;
							attachment.imageHeight = thumbnail->height;
						}
					}
					else if (missingThumbs != nullptr)
					{
						missingThumbs->insert(attachment.id);   // cần tải về
					}
				}

				// Dòng liên kết: tên tệp + dung lượng, bấm vào để tải bản gốc.
				CString label;

				if (imageShown)
				{
					label.Format(
						_T("%s (%s)"),
						static_cast<LPCTSTR>(FromUtf8(attachment.name)),
						static_cast<LPCTSTR>(FormatFileSize(attachment.size)));
				}
				else
				{
					label.Format(
						_T("%s %s (%s)"),
						attachment.kind == "video" ? _T("[Video]") : _T("[Ảnh]"),
						static_cast<LPCTSTR>(FromUtf8(attachment.name)),
						static_cast<LPCTSTR>(FormatFileSize(attachment.size)));
				}

				RichStyle linkStyle = bodyStyle;
				linkStyle.link = true;
				linkStyle.textColor = RGB(0, 102, 204);

				if (imageShown)
					linkStyle.sizePt = 9;

				const long linkStart = AppendParagraph(edit, label, linkStyle);

				if ((linkStart >= 0 || imageShown) && links != nullptr)
				{
					if (linkStart >= 0)
					{
						attachment.start = linkStart;
						attachment.end = linkStart + label.GetLength();
					}

					links->push_back(attachment);
				}
			}
			else
			{
				AppendParagraph(
					edit,
					CString(static_cast<LPCTSTR>(contentText)),
					bodyStyle);
			}
		}

		edit.SetSel(-1, -1);
		edit.SendMessage(EM_SCROLLCARET, 0, 0);
		edit.SendMessage(WM_VSCROLL, SB_BOTTOM, 0);

		edit.SetReadOnly(TRUE);

		edit.SetRedraw(TRUE);
		edit.Invalidate();
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

	m_lastHistoryPayload = payload;
	m_lastHistoryWasGroup = false;

	m_attachmentLinks.clear();

	std::set<int64_t> missingThumbs;

	RenderMessages(
		edit, response["messages"], m_currentUser,
		&m_attachmentLinks, &m_thumbs, &missingThumbs);

	RequestThumbnails(missingThumbs);
}

void CMFCChatDlg::DisplayGroupHistoryResponse(
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

	const int64_t groupId = response.value("groupId", int64_t(0));

	// Bỏ qua kết quả của nhóm khác (người dùng đã chuyển hội thoại).
	if (!m_isGroupChat || groupId != m_currentGroupId)
		return;

	CRichEditCtrl* pEdit =
		static_cast<CRichEditCtrl*>(GetDlgItem(IDC_RICHEDIT_MESSAGE));

	if (pEdit == nullptr)
		return;

	m_lastHistoryPayload = payload;
	m_lastHistoryWasGroup = true;

	m_attachmentLinks.clear();

	std::set<int64_t> missingThumbs;

	RenderMessages(
		*pEdit, response["messages"], m_currentUser,
		&m_attachmentLinks, &m_thumbs, &missingThumbs);

	RequestThumbnails(missingThumbs);
}

void CMFCChatDlg::DisplayGroupsResponse(
	const std::string& payload)
{
	using json = nlohmann::json;

	try
	{
		json response = json::parse(payload);

		if (!response.value("success", false) ||
			!response.contains("groups") ||
			!response["groups"].is_array())
		{
			return;
		}

		m_groupItems.clear();

		for (const auto& item : response["groups"])
		{
			ConversationItem row;

			row.groupId = item.value("groupId", int64_t(0));
			row.name = FromUtf8(item.value("groupName", std::string()));
			row.preview = MakePreview(item.value("lastMessage", std::string()));
			row.time = FromUtf8(item.value("lastSentAt", std::string()));

			if (row.groupId > 0)
				m_groupItems.push_back(row);
		}
	}
	catch (const json::exception&)
	{
		return;
	}

	RebuildConversationList();
}

// Dựng lại danh sách bên trái: nhóm ở trên, người dùng ở dưới.
void CMFCChatDlg::RebuildConversationList()
{
	const bool previousIsGroup = m_isGroupChat;
	const int64_t previousGroupId = m_currentGroupId;
	const CString previousUser = m_currentChatUser;

	m_rebuildingList = true;

	m_conversationList.SetRedraw(FALSE);
	m_conversationList.DeleteAllItems();
	m_fakeChats.RemoveAll();

	int selectedIndex = -1;

	auto addRow = [&](const ConversationItem& item, bool isGroup)
	{
		CString title = isGroup
			? CString(kGroupPrefix) + item.name
			: item.name;

		const int index = m_conversationList.InsertItem(
			m_conversationList.GetItemCount(),
			title);

		m_conversationList.SetItemData(
			index,
			isGroup ? static_cast<DWORD_PTR>(item.groupId) : 0);

		if (isGroup)
		{
			if (previousIsGroup && item.groupId == previousGroupId)
				selectedIndex = index;
		}
		else
		{
			m_fakeChats[item.name] = item.preview;

			if (!previousIsGroup && item.name == previousUser)
				selectedIndex = index;
		}
	};

	for (const auto& group : m_groupItems)
		addRow(group, true);

	for (const auto& user : m_userItems)
		addRow(user, false);

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
			LVIS_SELECTED | LVIS_FOCUSED);

		m_conversationList.EnsureVisible(selectedIndex, FALSE);
	}

	m_conversationList.SetRedraw(TRUE);
	FitConversationColumn();
	m_conversationList.Invalidate();

	m_rebuildingList = false;

	if (selectedIndex >= 0)
	{
		ApplySelection(selectedIndex);
		LoadChatHistory();
	}
}

// Cập nhật trạng thái "đang chat với ai" theo dòng được chọn.
void CMFCChatDlg::ApplySelection(int index)
{
	if (index < 0 || index >= m_conversationList.GetItemCount())
		return;

	const DWORD_PTR data = m_conversationList.GetItemData(index);
	const CString text = m_conversationList.GetItemText(index, 0);

	if (data != 0)
	{
		const int prefixLength = static_cast<int>(_tcslen(kGroupPrefix));

		m_isGroupChat = true;
		m_currentGroupId = static_cast<int64_t>(data);

		m_currentChatUser = (text.Left(prefixLength) == kGroupPrefix)
			? text.Mid(prefixLength)
			: text;

		SetDlgItemText(IDC_STATIC_CHAT_USER, m_currentChatUser);
		SetDlgItemText(IDC_STATIC_SUBTITLE, _T("Nhóm chat"));
	}
	else
	{
		m_isGroupChat = false;
		m_currentGroupId = 0;
		m_currentChatUser = text;

		SetDlgItemText(IDC_STATIC_CHAT_USER, m_currentChatUser);
		SetDlgItemText(IDC_STATIC_SUBTITLE, _T("Chat riêng"));
	}

	if (m_btnPeerAvatar.GetSafeHwnd() != nullptr)
		m_btnPeerAvatar.Invalidate();
}

void CMFCChatDlg::OnBnClickedCreateGroup()
{
	if (!m_tcpClient.IsConnected())
	{
		AfxMessageBox(L"Not connected to TCP server.");
		return;
	}

	std::vector<CString> candidates;

	for (const auto& user : m_userItems)
		candidates.push_back(user.name);

	if (candidates.empty())
	{
		AfxMessageBox(L"Chưa có người dùng nào khác để thêm vào nhóm.");
		return;
	}

	CCreateGroupDlg dlg(candidates, this);

	if (dlg.DoModal() != IDOK)
		return;

	CT2A nameUtf8(dlg.GetGroupName(), CP_UTF8);
	std::string groupName(static_cast<const char*>(nameUtf8));

	std::vector<std::string> members;

	for (const CString& member : dlg.GetSelectedMembers())
	{
		CT2A memberUtf8(member, CP_UTF8);
		members.emplace_back(static_cast<const char*>(memberUtf8));
	}

	if (!m_tcpClient.CreateGroup(groupName, members))
	{
		AfxMessageBox(L"Không gửi được yêu cầu tạo nhóm.");
	}
}

// Enter trong ô nhập = gửi tin (trước đây Enter sẽ đóng cửa sổ chat).
void CMFCChatDlg::OnOK()
{
	CWnd* focus = GetFocus();
	CWnd* input = GetDlgItem(IDC_EDIT_MESSAGE);

	if (focus != nullptr && input != nullptr &&
		focus->GetSafeHwnd() == input->GetSafeHwnd())
	{
		OnBnClickedButtonSend();
	}
}

// Áp dụng giao diện: danh sách một cột, nhãn tên, ô nhập, nút Gửi.
void CMFCChatDlg::ApplyChatTheme()
{
	CClientDC dc(this);
	const int dpi = dc.GetDeviceCaps(LOGPIXELSY);

	auto makeFont = [&](CFont& font, int pt, bool bold)
	{
		if (font.GetSafeHandle() != nullptr)
			return;

		font.CreateFont(
			-MulDiv(pt, dpi, 72), 0, 0, 0,
			bold ? FW_BOLD : FW_NORMAL,
			FALSE, FALSE, FALSE,
			DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY,
			DEFAULT_PITCH | FF_DONTCARE,
			_T("Segoe UI"));
	};

	makeFont(m_fontListName, 10, true);
	makeFont(m_fontListPreview, 9, false);
	makeFont(m_fontTitle, 13, true);
	makeFont(m_fontSubtitle, 9, false);
	makeFont(m_fontInput, 10, false);

	// ---- Danh sách bên trái: một cột, mỗi dòng 2 hàng (tên + tin cuối) ----
	const int rowHeight = 52;

	m_conversationList.ModifyStyle(0, LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS);

	m_conversationList.SetExtendedStyle(
		m_conversationList.GetExtendedStyle() |
		LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	if (m_rowHeightImages.GetSafeHandle() == nullptr)
	{
		// Thủ thuật quen thuộc: chiều cao dòng của ListView theo chiều cao
		// image list, nên tạo một image list rỗng cao rowHeight pixel.
		CBitmap bitmap;
		bitmap.CreateCompatibleBitmap(&dc, 1, rowHeight);

		m_rowHeightImages.Create(1, rowHeight, ILC_COLOR24, 1, 1);
		m_rowHeightImages.Add(&bitmap, RGB(255, 255, 255));
	}

	m_conversationList.SetImageList(&m_rowHeightImages, LVSIL_SMALL);
	m_conversationList.SetBkColor(RGB(255, 255, 255));

	// Chỉ giữ một cột; tên/tin cuối được tự vẽ từ m_groupItems/m_userItems.
	m_conversationList.DeleteColumn(2);
	m_conversationList.DeleteColumn(1);

	FitConversationColumn();

	::SetWindowTheme(m_conversationList.GetSafeHwnd(), L"Explorer", nullptr);

	// ---- Khung chat: thanh cuộn kiểu mới ----
	if (CWnd* chat = GetDlgItem(IDC_RICHEDIT_MESSAGE))
		::SetWindowTheme(chat->GetSafeHwnd(), L"Explorer", nullptr);

	// ---- Nhãn tên: bỏ viền lõm, chữ to đậm + dòng phụ nhỏ ----
	if (CWnd* title = GetDlgItem(IDC_STATIC_CHAT_USER))
	{
		CRect rect;
		title->GetWindowRect(&rect);
		ScreenToClient(&rect);

		title->ModifyStyle(WS_BORDER | SS_SUNKEN, 0, SWP_FRAMECHANGED);
		title->ModifyStyleEx(
			WS_EX_CLIENTEDGE | WS_EX_STATICEDGE |
			WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME,
			0, SWP_FRAMECHANGED);

		title->SetFont(&m_fontTitle);
		title->MoveWindow(
			rect.left + 2, rect.top + 2, rect.Width() - 2, 26);

		if (m_staticSubtitle.GetSafeHwnd() == nullptr)
		{
			m_staticSubtitle.Create(
				_T(""),
				WS_CHILD | WS_VISIBLE | SS_LEFT,
				CRect(rect.left + 2, rect.top + 30,
					rect.right, rect.top + 48),
				this,
				IDC_STATIC_SUBTITLE);

			m_staticSubtitle.SetFont(&m_fontSubtitle);
		}

		Invalidate();
	}

	// ---- Ô nhập: chữ gợi ý + font ----
	if (CEdit* input = static_cast<CEdit*>(GetDlgItem(IDC_EDIT_MESSAGE)))
	{
		input->SetFont(&m_fontInput);
		input->SetCueBanner(_T("Nhập tin nhắn…"), TRUE);
	}

	// ---- Nút Gửi: phẳng, màu xanh (vẽ trong OnDrawItem) ----
	if (CWnd* send = GetDlgItem(IDC_BUTTON_SEND))
	{
		send->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
		SetDlgItemText(IDC_BUTTON_SEND, _T("Gửi"));
		send->Invalidate();
	}
}

// Vẽ nút Gửi: nền xanh phẳng bo góc, chữ trắng.
void CMFCChatDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDIS)
{
	// Ô avatar ở hàng tiêu đề (người đối diện / của mình).
	if (lpDIS != nullptr &&
		(nIDCtl == IDC_BUTTON_PEER_AVATAR || nIDCtl == IDC_BUTTON_MY_AVATAR))
	{
		CDC* dc = CDC::FromHandle(lpDIS->hDC);
		CRect area(lpDIS->rcItem);

		dc->FillSolidRect(area, ::GetSysColor(COLOR_BTNFACE));

		const bool peer = (nIDCtl == IDC_BUTTON_PEER_AVATAR);
		const CString& label = peer ? m_currentChatUser : m_currentUser;

		DrawAvatar(
			lpDIS->hDC, area, label, ToUtf8(label), peer && m_isGroupChat);
		return;
	}

	// Nút "Ảnh/Video": nền xám nhạt, viền mảnh.
	if (lpDIS != nullptr && nIDCtl == IDC_BUTTON_ATTACH)
	{
		CDC* dc = CDC::FromHandle(lpDIS->hDC);
		CRect area(lpDIS->rcItem);

		const bool pressed = (lpDIS->itemState & ODS_SELECTED) != 0;
		const COLORREF fill = pressed ? RGB(214, 220, 229) : RGB(235, 238, 243);
		const COLORREF border = RGB(196, 202, 212);

		dc->FillSolidRect(area, ::GetSysColor(COLOR_BTNFACE));

		CBrush brush(fill);
		CPen pen(PS_SOLID, 1, border);

		CBrush* oldBrush = dc->SelectObject(&brush);
		CPen* oldPen = dc->SelectObject(&pen);

		dc->RoundRect(area, CPoint(8, 8));

		dc->SelectObject(oldBrush);
		dc->SelectObject(oldPen);

		CString text;
		GetDlgItemText(IDC_BUTTON_ATTACH, text);

		CFont* oldFont = dc->SelectObject(GetFont());

		dc->SetBkMode(TRANSPARENT);
		dc->SetTextColor(RGB(45, 55, 72));
		dc->DrawText(text, area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		dc->SelectObject(oldFont);
		return;
	}

	if (nIDCtl != IDC_BUTTON_SEND || lpDIS == nullptr)
	{
		CDialogEx::OnDrawItem(nIDCtl, lpDIS);
		return;
	}

	CDC* pDC = CDC::FromHandle(lpDIS->hDC);
	CRect rect(lpDIS->rcItem);

	const bool pressed = (lpDIS->itemState & ODS_SELECTED) != 0;
	const bool disabled = (lpDIS->itemState & ODS_DISABLED) != 0;

	const COLORREF fill = disabled
		? RGB(190, 200, 215)
		: (pressed ? RGB(0, 84, 166) : RGB(0, 120, 215));

	pDC->FillSolidRect(rect, ::GetSysColor(COLOR_BTNFACE));

	CBrush brush(fill);
	CPen pen(PS_SOLID, 1, fill);

	CBrush* oldBrush = pDC->SelectObject(&brush);
	CPen* oldPen = pDC->SelectObject(&pen);

	pDC->RoundRect(rect, CPoint(8, 8));

	pDC->SelectObject(oldBrush);
	pDC->SelectObject(oldPen);

	CString text;
	GetDlgItemText(IDC_BUTTON_SEND, text);

	CFont* oldFont = pDC->SelectObject(GetFont());

	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(RGB(255, 255, 255));
	pDC->DrawText(text, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

	pDC->SelectObject(oldFont);
}

// Dòng phụ của nhãn tên có màu xám.
HBRUSH CMFCChatDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH brush = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

	if (pWnd != nullptr && pWnd->GetDlgCtrlID() == IDC_STATIC_SUBTITLE)
		pDC->SetTextColor(RGB(120, 120, 120));

	return brush;
}

// Tự vẽ từng dòng của danh sách hội thoại: tên (đậm) + tin cuối (xám).
void CMFCChatDlg::OnCustomDrawConversationList(
	NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLVCUSTOMDRAW draw = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);

	*pResult = CDRF_DODEFAULT;

	if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;
	}

	if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT)
		return;

	const int index = static_cast<int>(draw->nmcd.dwItemSpec);

	CDC* pDC = CDC::FromHandle(draw->nmcd.hdc);

	CRect clientRect;
	m_conversationList.GetClientRect(&clientRect);

	CRect rect(draw->nmcd.rc);
	rect.left = clientRect.left;
	rect.right = clientRect.right;

	const bool selected =
		(m_conversationList.GetItemState(index, LVIS_SELECTED)
			& LVIS_SELECTED) != 0;

	pDC->FillSolidRect(
		rect, selected ? RGB(222, 235, 252) : RGB(255, 255, 255));

	if (selected)
		pDC->FillSolidRect(rect.left, rect.top, 3, rect.Height(), RGB(0, 120, 215));

	// đường kẻ mảnh ngăn cách các dòng
	pDC->FillSolidRect(
		rect.left + 12, rect.bottom - 1, rect.Width() - 24, 1,
		RGB(238, 238, 238));

	const CString name = m_conversationList.GetItemText(index, 0);

	// Thứ tự dòng trong danh sách: nhóm trước, người dùng sau.
	CString preview;
	const size_t row = static_cast<size_t>(index);

	if (row < m_groupItems.size())
	{
		preview = m_groupItems[row].preview;
	}
	else if (row - m_groupItems.size() < m_userItems.size())
	{
		preview = m_userItems[row - m_groupItems.size()].preview;
	}

	// Avatar tròn bên trái; chữ nằm bên phải avatar.
	const bool isGroupRow = row < m_groupItems.size();

	CString avatarLabel = name;
	const int prefixLength = static_cast<int>(_tcslen(kGroupPrefix));

	if (isGroupRow && avatarLabel.Left(prefixLength) == kGroupPrefix)
		avatarLabel = avatarLabel.Mid(prefixLength);

	DrawAvatar(
		pDC->GetSafeHdc(),
		CRect(rect.left + 10, rect.top + 8, rect.left + 46, rect.top + 44),
		avatarLabel,
		isGroupRow ? std::string() : ToUtf8(name),
		isGroupRow);

	CRect nameRect(rect.left + 54, rect.top + 8, rect.right - 8, rect.top + 28);
	CRect previewRect(rect.left + 54, rect.top + 29, rect.right - 8, rect.bottom - 6);

	pDC->SetBkMode(TRANSPARENT);

	CFont* oldFont = pDC->GetCurrentFont();

	if (m_fontListName.GetSafeHandle() != nullptr)
		pDC->SelectObject(&m_fontListName);

	pDC->SetTextColor(RGB(25, 25, 25));
	pDC->DrawText(
		name, nameRect,
		DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

	if (m_fontListPreview.GetSafeHandle() != nullptr)
		pDC->SelectObject(&m_fontListPreview);

	pDC->SetTextColor(RGB(120, 120, 120));
	pDC->DrawText(
		preview, previewRect,
		DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);

	pDC->SelectObject(oldFont);

	*pResult = CDRF_SKIPDEFAULT;
}

namespace
{
	const DWORD kAnchorLeft = 1;
	const DWORD kAnchorTop = 2;
	const DWORD kAnchorRight = 4;
	const DWORD kAnchorBottom = 8;
}

// Ghi nhớ vị trí ban đầu và cạnh bám của từng control.
void CMFCChatDlg::RegisterAnchors()
{
	struct Spec
	{
		UINT id;
		DWORD anchors;
	};

	const Spec specs[] =
	{
		{ IDC_LIST_CONVERSATION,   kAnchorLeft | kAnchorTop | kAnchorBottom },
		{ IDC_BUTTON_CREATE_GROUP, kAnchorLeft | kAnchorBottom },
		{ IDC_STATIC_CHAT_USER,    kAnchorLeft | kAnchorTop | kAnchorRight },
		{ IDC_STATIC_SUBTITLE,     kAnchorLeft | kAnchorTop | kAnchorRight },
		{ IDC_RICHEDIT_MESSAGE,    kAnchorLeft | kAnchorTop | kAnchorRight | kAnchorBottom },
		{ IDC_EDIT_MESSAGE,        kAnchorLeft | kAnchorRight | kAnchorBottom },
		{ IDC_BUTTON_SEND,         kAnchorRight | kAnchorBottom },
		{ IDC_BUTTON_ATTACH,       kAnchorRight | kAnchorBottom },
		{ IDC_BUTTON_PEER_AVATAR,  kAnchorLeft | kAnchorTop },
		{ IDC_BUTTON_MY_AVATAR,    kAnchorTop | kAnchorRight },
	};

	CRect client;
	GetClientRect(&client);
	m_initialClientSize = client.Size();

	CRect window;
	GetWindowRect(&window);
	m_minWindowSize = window.Size();

	m_anchors.clear();

	for (const Spec& spec : specs)
	{
		CWnd* control = GetDlgItem(spec.id);

		if (control == nullptr)
			continue;

		AnchorInfo info;
		info.id = spec.id;
		info.anchors = spec.anchors;

		control->GetWindowRect(&info.initial);
		ScreenToClient(&info.initial);

		m_anchors.push_back(info);
	}

	m_layoutReady = true;
}

// Đặt lại vị trí/kích thước các control theo kích thước client mới.
void CMFCChatDlg::LayoutControls(int cx, int cy)
{
	const int dx = cx - m_initialClientSize.cx;
	const int dy = cy - m_initialClientSize.cy;

	for (const AnchorInfo& info : m_anchors)
	{
		CWnd* control = GetDlgItem(info.id);

		if (control == nullptr)
			continue;

		CRect rect = info.initial;

		if (info.anchors & kAnchorRight)
		{
			rect.right += dx;

			if (!(info.anchors & kAnchorLeft))
				rect.left += dx;
		}

		if (info.anchors & kAnchorBottom)
		{
			rect.bottom += dy;

			if (!(info.anchors & kAnchorTop))
				rect.top += dy;
		}

		// SWP_NOCOPYBITS: không sao chép nội dung cũ sang vị trí mới.
		// Nếu không, các nút tự vẽ (Gửi, Ảnh/Video) để lại vệt khi kéo cửa sổ.
		control->SetWindowPos(
			nullptr,
			rect.left, rect.top, rect.Width(), rect.Height(),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	}

	FitConversationColumn();

	// Dialog có WS_CLIPCHILDREN nên phải yêu cầu vẽ lại cả các control con.
	RedrawWindow(
		nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CMFCChatDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialogEx::OnSize(nType, cx, cy);

	if (!m_layoutReady || nType == SIZE_MINIMIZED || cx <= 0 || cy <= 0)
		return;

	LayoutControls(cx, cy);
}

// Không cho thu nhỏ cửa sổ nhỏ hơn kích thước thiết kế ban đầu.
void CMFCChatDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDialogEx::OnGetMinMaxInfo(lpMMI);

	if (m_layoutReady && lpMMI != nullptr)
	{
		lpMMI->ptMinTrackSize.x = m_minWindowSize.cx;
		lpMMI->ptMinTrackSize.y = m_minWindowSize.cy;
	}
}

// Cột duy nhất của danh sách luôn rộng đúng bằng vùng client,
// để mỗi dòng phủ kín chiều ngang và không có thanh cuộn ngang.
void CMFCChatDlg::FitConversationColumn()
{
	if (m_conversationList.GetSafeHwnd() == nullptr)
		return;

	CRect rect;
	m_conversationList.GetClientRect(&rect);

	if (rect.Width() > 0)
		m_conversationList.SetColumnWidth(0, rect.Width());

	m_conversationList.ShowScrollBar(SB_HORZ, FALSE);
}

// ====================== Avatar, gửi ảnh/video, tải tệp ======================

namespace
{
	bool GetEncoderClsid(const WCHAR* mimeType, CLSID* clsid)
	{
		UINT count = 0;
		UINT bytes = 0;

		if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok ||
			bytes == 0)
		{
			return false;
		}

		std::vector<BYTE> buffer(bytes);

		Gdiplus::ImageCodecInfo* codecs =
			reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());

		if (Gdiplus::GetImageEncoders(count, bytes, codecs) != Gdiplus::Ok)
			return false;

		for (UINT i = 0; i < count; ++i)
		{
			if (wcscmp(codecs[i].MimeType, mimeType) == 0)
			{
				*clsid = codecs[i].Clsid;
				return true;
			}
		}

		return false;
	}

	// Trong bản Debug, MFC định nghĩa "new" thành DEBUG_NEW (thêm tham số file/dòng),
	// nhưng các lớp GDI+ không có operator new tương ứng -> lỗi C2660.
	// Tạm bỏ macro này cho hàm dưới đây.
#pragma push_macro("new")
#undef new
	// Giải mã PNG trong bộ nhớ thành Gdiplus::Bitmap độc lập với stream.
	std::shared_ptr<void> BitmapFromPngBytes(const std::string& png)
	{
		if (png.empty())
			return nullptr;

		IStream* stream = SHCreateMemStream(
			reinterpret_cast<const BYTE*>(png.data()),
			static_cast<UINT>(png.size()));

		if (stream == nullptr)
			return nullptr;

		Gdiplus::Bitmap* copy = nullptr;

		{
			Gdiplus::Bitmap source(stream);

			if (source.GetLastStatus() == Gdiplus::Ok &&
				source.GetWidth() > 0 &&
				source.GetHeight() > 0)
			{
				copy = new Gdiplus::Bitmap(
					source.GetWidth(),
					source.GetHeight(),
					PixelFormat32bppARGB);

				Gdiplus::Graphics graphics(copy);
				graphics.DrawImage(
					&source, 0, 0,
					static_cast<INT>(source.GetWidth()),
					static_cast<INT>(source.GetHeight()));
			}
		}   // source được hủy trước khi giải phóng stream

		stream->Release();

		if (copy == nullptr)
			return nullptr;

		return std::shared_ptr<void>(
			copy,
			[](void* p) { delete static_cast<Gdiplus::Bitmap*>(p); });
	}
#pragma pop_macro("new")

	// Đọc ảnh bất kỳ, cắt vuông ở giữa, thu về 128x128 rồi mã hóa PNG.
	bool PrepareAvatarPng(const CString& path, std::string& png)
	{
		png.clear();

		Gdiplus::Bitmap source(static_cast<LPCTSTR>(path));

		if (source.GetLastStatus() != Gdiplus::Ok ||
			source.GetWidth() == 0 ||
			source.GetHeight() == 0)
		{
			return false;
		}

		const INT width = static_cast<INT>(source.GetWidth());
		const INT height = static_cast<INT>(source.GetHeight());
		const INT side = (width < height) ? width : height;
		const INT cropX = (width - side) / 2;
		const INT cropY = (height - side) / 2;

		const INT outputSize = 128;

		Gdiplus::Bitmap output(outputSize, outputSize, PixelFormat32bppARGB);

		{
			Gdiplus::Graphics graphics(&output);
			graphics.SetInterpolationMode(
				Gdiplus::InterpolationModeHighQualityBicubic);
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
			graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

			graphics.DrawImage(
				&source,
				Gdiplus::Rect(0, 0, outputSize, outputSize),
				cropX, cropY, side, side,
				Gdiplus::UnitPixel);
		}

		CLSID pngClsid;

		if (!GetEncoderClsid(L"image/png", &pngClsid))
			return false;

		IStream* stream = nullptr;

		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
			stream == nullptr)
		{
			return false;
		}

		bool ok = false;

		if (output.Save(stream, &pngClsid, nullptr) == Gdiplus::Ok)
		{
			STATSTG stat = {};

			if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)))
			{
				const ULONG size = static_cast<ULONG>(stat.cbSize.QuadPart);

				LARGE_INTEGER start = {};
				stream->Seek(start, STREAM_SEEK_SET, nullptr);

				png.resize(size);
				ULONG read = 0;

				ok = SUCCEEDED(stream->Read(&png[0], size, &read)) &&
					read == size && size > 0;
			}
		}

		stream->Release();

		if (!ok)
			png.clear();

		return ok;
	}

	// Đọc toàn bộ nội dung một IStream (đã ghi xong) vào chuỗi.
	bool StreamToString(IStream* stream, std::string& output)
	{
		output.clear();

		STATSTG stat = {};

		if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)))
			return false;

		const ULONG size = static_cast<ULONG>(stat.cbSize.QuadPart);

		if (size == 0)
			return false;

		LARGE_INTEGER start = {};
		stream->Seek(start, STREAM_SEEK_SET, nullptr);

		output.resize(size);

		ULONG read = 0;

		if (FAILED(stream->Read(&output[0], size, &read)) || read != size)
		{
			output.clear();
			return false;
		}

		return true;
	}

	// Tạo ảnh thu nhỏ JPEG (tối đa 240 px mỗi cạnh) từ ảnh gốc.
	bool PrepareThumbnailJpeg(const CString& path, std::string& jpeg)
	{
		jpeg.clear();

		Gdiplus::Bitmap source(static_cast<LPCTSTR>(path));

		if (source.GetLastStatus() != Gdiplus::Ok ||
			source.GetWidth() == 0 ||
			source.GetHeight() == 0)
		{
			return false;
		}

		const double maxSide = 240.0;
		const double width = static_cast<double>(source.GetWidth());
		const double height = static_cast<double>(source.GetHeight());

		double scale = 1.0;   // không phóng to ảnh nhỏ

		if (maxSide / width < scale)
			scale = maxSide / width;

		if (maxSide / height < scale)
			scale = maxSide / height;

		INT outWidth = static_cast<INT>(width * scale + 0.5);
		INT outHeight = static_cast<INT>(height * scale + 0.5);

		if (outWidth < 1) outWidth = 1;
		if (outHeight < 1) outHeight = 1;

		Gdiplus::Bitmap output(outWidth, outHeight, PixelFormat24bppRGB);

		{
			Gdiplus::Graphics graphics(&output);
			graphics.SetInterpolationMode(
				Gdiplus::InterpolationModeHighQualityBicubic);
			graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
			graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

			// Nền trắng cho ảnh PNG/GIF có vùng trong suốt.
			graphics.Clear(Gdiplus::Color(255, 255, 255, 255));

			graphics.DrawImage(
				&source,
				Gdiplus::Rect(0, 0, outWidth, outHeight),
				0, 0,
				static_cast<INT>(source.GetWidth()),
				static_cast<INT>(source.GetHeight()),
				Gdiplus::UnitPixel);
		}

		CLSID jpegClsid;

		if (!GetEncoderClsid(L"image/jpeg", &jpegClsid))
			return false;

		ULONG quality = 82;

		Gdiplus::EncoderParameters parameters;
		parameters.Count = 1;
		parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
		parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
		parameters.Parameter[0].NumberOfValues = 1;
		parameters.Parameter[0].Value = &quality;

		IStream* stream = nullptr;

		if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
			stream == nullptr)
		{
			return false;
		}

		bool ok = output.Save(stream, &jpegClsid, &parameters) == Gdiplus::Ok &&
			StreamToString(stream, jpeg);

		stream->Release();

		// Server giới hạn 128 KB cho ảnh thu nhỏ.
		if (!ok || jpeg.size() > 120 * 1024)
		{
			jpeg.clear();
			return false;
		}

		return true;
	}

	// Giải mã ảnh thu nhỏ JPEG thành HBITMAP (người gọi sở hữu và phải xóa).
	bool DecodeThumbnail(const std::string& jpeg, ChatThumbnail& out)
	{
		if (jpeg.empty())
			return false;

		IStream* stream = SHCreateMemStream(
			reinterpret_cast<const BYTE*>(jpeg.data()),
			static_cast<UINT>(jpeg.size()));

		if (stream == nullptr)
			return false;

		HBITMAP bitmap = nullptr;
		int width = 0;
		int height = 0;
		bool ok = false;

		{
			Gdiplus::Bitmap source(stream);

			width = static_cast<int>(source.GetWidth());
			height = static_cast<int>(source.GetHeight());

			// Chặn ảnh có kích thước bất thường (dữ liệu do người khác gửi).
			if (source.GetLastStatus() == Gdiplus::Ok &&
				width > 0 && width <= 1024 &&
				height > 0 && height <= 1024)
			{
				ok = source.GetHBITMAP(
					Gdiplus::Color(255, 250, 250, 252), &bitmap) == Gdiplus::Ok &&
					bitmap != nullptr;
			}
		}

		stream->Release();

		if (!ok)
			return false;

		out.bitmap = bitmap;
		out.width = width;
		out.height = height;
		out.jpeg = jpeg;

		return true;
	}

	// Thư mục lưu tệp đã tải: %LOCALAPPDATA%\MFCChat\cache
	CString CacheDirectory()
	{
		TCHAR base[MAX_PATH] = {};
		const DWORD length =
			::ExpandEnvironmentStrings(_T("%LOCALAPPDATA%"), base, MAX_PATH);

		CString directory;

		if (length == 0 || length > MAX_PATH ||
			_tcschr(base, _T('%')) != nullptr)
		{
			TCHAR temp[MAX_PATH] = {};
			::GetTempPath(MAX_PATH, temp);

			directory = temp;
			directory.TrimRight(_T('\\'));
		}
		else
		{
			directory = base;
		}

		directory += _T("\\MFCChat");
		::CreateDirectory(directory, nullptr);

		directory += _T("\\cache");
		::CreateDirectory(directory, nullptr);

		return directory;
	}

	CString SafeFileName(const CString& name)
	{
		CString result = name;

		const TCHAR invalid[] = _T("\\/:*?\"<>|");

		for (int i = 0; i < result.GetLength(); ++i)
		{
			if (result[i] < 32 || _tcschr(invalid, result[i]) != nullptr)
				result.SetAt(i, _T('_'));
		}

		return result;
	}

	// "image" / "video" theo đuôi file; false nếu không được hỗ trợ.
	bool ClassifyMediaFile(const CString& path, std::string& kind)
	{
		const int dot = path.ReverseFind(_T('.'));

		if (dot < 0)
			return false;

		CString extension = path.Mid(dot);
		extension.MakeLower();

		static const TCHAR* imageExts[] =
		{ _T(".jpg"), _T(".jpeg"), _T(".png"), _T(".gif"),
		  _T(".bmp"), _T(".webp") };

		static const TCHAR* videoExts[] =
		{ _T(".mp4"), _T(".webm"), _T(".mov"), _T(".avi"),
		  _T(".mkv"), _T(".wmv") };

		for (const TCHAR* ext : imageExts)
		{
			if (extension == ext)
			{
				kind = "image";
				return true;
			}
		}

		for (const TCHAR* ext : videoExts)
		{
			if (extension == ext)
			{
				kind = "video";
				return true;
			}
		}

		return false;
	}

	bool GetFileSize64(const CString& path, int64_t& size)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};

		if (!::GetFileAttributesEx(path, GetFileExInfoStandard, &data))
			return false;

		size = (static_cast<int64_t>(data.nFileSizeHigh) << 32) |
			static_cast<int64_t>(data.nFileSizeLow);

		return true;
	}

	const int64_t kMaxImageBytes = 10LL * 1024 * 1024;
	const int64_t kMaxVideoBytes = 100LL * 1024 * 1024;
	const size_t kUploadChunkBytes = 192 * 1024;
}

// ---------------------------------------------------------------- Giao diện

// Mở rộng danh sách, thêm nút Ảnh/Video và ô avatar.
void CMFCChatDlg::ApplyMediaUi()
{
	auto getRect = [this](UINT id, CRect& rect) -> CWnd*
	{
		CWnd* control = GetDlgItem(id);

		if (control != nullptr)
		{
			control->GetWindowRect(&rect);
			ScreenToClient(&rect);
		}

		return control;
	};

	auto setRect = [](CWnd* control, const CRect& rect)
	{
		if (control != nullptr)
		{
			control->SetWindowPos(
				nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
	};

	CRect listRect, createRect, titleRect, subtitleRect;
	CRect chatRect, inputRect, sendRect;

	CWnd* list = getRect(IDC_LIST_CONVERSATION, listRect);
	CWnd* create = getRect(IDC_BUTTON_CREATE_GROUP, createRect);
	CWnd* title = getRect(IDC_STATIC_CHAT_USER, titleRect);
	CWnd* subtitle = getRect(IDC_STATIC_SUBTITLE, subtitleRect);
	CWnd* chat = getRect(IDC_RICHEDIT_MESSAGE, chatRect);
	CWnd* input = getRect(IDC_EDIT_MESSAGE, inputRect);
	CWnd* send = getRect(IDC_BUTTON_SEND, sendRect);

	if (list == nullptr || title == nullptr || subtitle == nullptr ||
		chat == nullptr || input == nullptr || send == nullptr)
	{
		return;
	}

	// Danh sách rộng hơn để vừa avatar + tên; khu vực chat hẹp lại tương ứng.
	const int listExtraWidth = 70;

	listRect.right += listExtraWidth;
	createRect.right += listExtraWidth;

	titleRect.left += listExtraWidth;
	subtitleRect.left += listExtraWidth;
	chatRect.left += listExtraWidth;
	inputRect.left += listExtraWidth;

	// Hàng tiêu đề: [avatar người đối diện] tên ... [avatar của mình]
	const int avatarSize = 40;
	const int avatarGap = 8;

	const CRect peerRect(
		titleRect.left, titleRect.top,
		titleRect.left + avatarSize, titleRect.top + avatarSize);

	titleRect.left += avatarSize + avatarGap;
	subtitleRect.left += avatarSize + avatarGap;

	const CRect myRect(
		chatRect.right - avatarSize, titleRect.top,
		chatRect.right, titleRect.top + avatarSize);

	titleRect.right = myRect.left - avatarGap;
	subtitleRect.right = myRect.left - avatarGap;

	// Nút Ảnh/Video nằm giữa ô nhập và nút Gửi.
	const int attachWidth = 84;
	const int gap = 6;

	const CRect attachRect(
		sendRect.left - gap - attachWidth, sendRect.top,
		sendRect.left - gap, sendRect.bottom);

	inputRect.right = attachRect.left - gap;

	setRect(list, listRect);
	setRect(create, createRect);
	setRect(title, titleRect);
	setRect(subtitle, subtitleRect);
	setRect(chat, chatRect);     // dời khung chat sang phải, hết đè lên danh sách
	setRect(input, inputRect);

	m_btnAttach.Create(
		_T("Ảnh/Video"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
		attachRect, this, IDC_BUTTON_ATTACH);

	m_btnPeerAvatar.Create(
		_T(""),
		WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
		peerRect, this, IDC_BUTTON_PEER_AVATAR);

	m_btnMyAvatar.Create(
		_T(""),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
		myRect, this, IDC_BUTTON_MY_AVATAR);

	m_btnAttach.SetFont(GetFont());

	// Cho phép kéo thả tệp vào cửa sổ và bấm vào liên kết tệp trong khung chat.
	DragAcceptFiles(TRUE);

	if (CRichEditCtrl* richEdit = static_cast<CRichEditCtrl*>(chat))
	{
		// ENM_LINK: bấm tên tệp; ENM_MOUSEEVENTS: bấm vào ảnh thu nhỏ.
		richEdit->SetEventMask(
			richEdit->GetEventMask() | ENM_LINK | ENM_MOUSEEVENTS);

		// Cần OLE để chèn ảnh vào RichEdit.
		::OleInitialize(nullptr);

		if (m_oleCallback == nullptr)
		{
			m_oleCallback = new CRichEditOleCallback();
			richEdit->SetOLECallback(m_oleCallback);
		}
	}

	FitConversationColumn();
	Invalidate();
}

void CMFCChatDlg::SetStatusText(const CString& text)
{
	SetDlgItemText(IDC_STATIC_SUBTITLE, text);
}

void CMFCChatDlg::UpdateSubtitle()
{
	if (m_currentChatUser.IsEmpty())
		SetStatusText(_T(""));
	else
		SetStatusText(m_isGroupChat ? _T("Nhóm chat") : _T("Chat riêng"));
}

// ------------------------------------------------------------------ Avatar

void CMFCChatDlg::DrawAvatar(
	HDC hdc,
	const CRect& rect,
	const CString& label,
	const std::string& key,
	bool isGroup)
{
	if (m_gdiplusToken == 0 || hdc == nullptr || rect.Width() <= 0)
		return;

	Gdiplus::Graphics graphics(hdc);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
	graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

	const Gdiplus::Rect area(
		rect.left, rect.top, rect.Width(), rect.Height());

	// Có ảnh đại diện: cắt tròn rồi vẽ.
	if (!isGroup && !key.empty())
	{
		auto found = m_avatarBitmaps.find(key);

		if (found != m_avatarBitmaps.end() && found->second)
		{
			Gdiplus::GraphicsPath path;
			path.AddEllipse(area);

			graphics.SetClip(&path);
			graphics.DrawImage(
				static_cast<Gdiplus::Bitmap*>(found->second.get()), area);
			graphics.ResetClip();

			return;
		}
	}

	// Chưa có ảnh: vòng tròn màu theo tên + chữ cái đầu.
	static const COLORREF palette[] =
	{
		RGB(0, 120, 215),  RGB(16, 124, 16),  RGB(135, 100, 184),
		RGB(202, 80, 16),  RGB(0, 153, 188),  RGB(194, 57, 179),
		RGB(104, 118, 138), RGB(218, 59, 1)
	};

	size_t hash = 0;

	for (int i = 0; i < label.GetLength(); ++i)
		hash = hash * 31 + static_cast<size_t>(label[i]);

	const COLORREF color =
		palette[hash % (sizeof(palette) / sizeof(palette[0]))];

	Gdiplus::SolidBrush background(Gdiplus::Color(
		255, GetRValue(color), GetGValue(color), GetBValue(color)));

	graphics.FillEllipse(&background, area);

	CString initial = label.IsEmpty() ? CString(_T("?")) : label.Left(1);
	initial.MakeUpper();

	Gdiplus::FontFamily family(L"Segoe UI");

	Gdiplus::Font font(
		&family,
		static_cast<Gdiplus::REAL>(rect.Height()) * 0.45f,
		Gdiplus::FontStyleBold,
		Gdiplus::UnitPixel);

	Gdiplus::StringFormat format;
	format.SetAlignment(Gdiplus::StringAlignmentCenter);
	format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

	Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));

	graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

	graphics.DrawString(
		initial, -1, &font,
		Gdiplus::RectF(
			static_cast<Gdiplus::REAL>(rect.left),
			static_cast<Gdiplus::REAL>(rect.top),
			static_cast<Gdiplus::REAL>(rect.Width()),
			static_cast<Gdiplus::REAL>(rect.Height())),
		&format, &textBrush);
}

// version > 0: chỉ tải nếu khác bản đang có.
// version == -1: ảnh của chính mình (chưa biết version), tải một lần.
void CMFCChatDlg::RequestAvatarIfNeeded(
	const std::string& username,
	int version)
{
	if (username.empty() || version == 0 || !m_tcpClient.IsConnected())
		return;

	if (version > 0)
	{
		auto known = m_avatarVersions.find(username);

		if (known != m_avatarVersions.end() && known->second == version)
			return;
	}

	const std::string requestKey =
		username + "#" + std::to_string(version);

	if (!m_avatarRequested.insert(requestKey).second)
		return;

	m_tcpClient.SendRaw(nlohmann::json{
		{"action", "GET_AVATAR"},
		{"username", username}
		}.dump());
}

void CMFCChatDlg::OnAvatarResult(const std::string& payload)
{
	using json = nlohmann::json;

	try
	{
		json response = json::parse(payload);

		if (!response.value("success", false))
			return;

		const std::string username =
			response.value("username", std::string());

		const int version = response.value("version", 0);

		const std::string encoded =
			response.value("data", std::string());

		if (username.empty())
			return;

		if (version <= 0 || encoded.empty())
		{
			m_avatarBitmaps.erase(username);
			m_avatarVersions.erase(username);
		}
		else
		{
			std::string png;

			if (!b64::Decode(encoded, png))
				return;

			std::shared_ptr<void> bitmap = BitmapFromPngBytes(png);

			if (!bitmap)
				return;

			m_avatarBitmaps[username] = bitmap;
			m_avatarVersions[username] = version;
		}
	}
	catch (const json::exception&)
	{
		return;
	}

	m_conversationList.Invalidate();

	if (m_btnPeerAvatar.GetSafeHwnd() != nullptr)
		m_btnPeerAvatar.Invalidate();

	if (m_btnMyAvatar.GetSafeHwnd() != nullptr)
		m_btnMyAvatar.Invalidate();
}

void CMFCChatDlg::OnSetAvatarResult(const std::string& payload)
{
	using json = nlohmann::json;

	bool success = false;
	int version = 0;
	std::string errorText = "Unknown error";

	try
	{
		json response = json::parse(payload);

		success = response.value("success", false);
		version = response.value("version", 0);

		if (!success)
		{
			errorText = response.value(
				"message", std::string("Unknown error"));
		}
	}
	catch (const json::exception&)
	{
		errorText = "Invalid response from server";
	}

	if (!success)
	{
		m_pendingAvatarPng.clear();

		CString message;
		message.Format(
			L"Không đổi được ảnh đại diện: %s",
			static_cast<LPCTSTR>(FromUtf8(errorText)));

		AfxMessageBox(message);
		return;
	}

	// Dùng luôn ảnh vừa gửi, không cần tải lại từ server.
	std::shared_ptr<void> bitmap = BitmapFromPngBytes(m_pendingAvatarPng);
	m_pendingAvatarPng.clear();

	if (bitmap)
	{
		const std::string key = ToUtf8(m_currentUser);

		m_avatarBitmaps[key] = bitmap;
		m_avatarVersions[key] = version;
	}

	m_conversationList.Invalidate();

	if (m_btnPeerAvatar.GetSafeHwnd() != nullptr)
		m_btnPeerAvatar.Invalidate();

	if (m_btnMyAvatar.GetSafeHwnd() != nullptr)
		m_btnMyAvatar.Invalidate();
}

void CMFCChatDlg::OnBnClickedMyAvatar()
{
	if (!m_tcpClient.IsConnected())
	{
		AfxMessageBox(L"Not connected to TCP server.");
		return;
	}

	CFileDialog dialog(
		TRUE, nullptr, nullptr,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"Ảnh (*.jpg;*.jpeg;*.png;*.bmp;*.gif)|*.jpg;*.jpeg;*.png;*.bmp;*.gif||",
		this);

	dialog.m_ofn.lpstrTitle = L"Chọn ảnh đại diện";

	if (dialog.DoModal() != IDOK)
		return;

	std::string png;

	if (!PrepareAvatarPng(dialog.GetPathName(), png))
	{
		AfxMessageBox(L"Không đọc được ảnh này.");
		return;
	}

	m_pendingAvatarPng = png;

	if (!m_tcpClient.SendRaw(nlohmann::json{
		{"action", "SET_AVATAR"},
		{"data", b64::Encode(png)}
		}.dump()))
	{
		m_pendingAvatarPng.clear();
		AfxMessageBox(L"Không gửi được ảnh đại diện.");
	}
}

// --------------------------------------------------------- Gửi ảnh / video

void CMFCChatDlg::OnBnClickedAttach()
{
	CFileDialog dialog(
		TRUE, nullptr, nullptr,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		L"Ảnh và video|*.jpg;*.jpeg;*.png;*.gif;*.bmp;*.webp;"
		L"*.mp4;*.webm;*.mov;*.avi;*.mkv;*.wmv|Tất cả tệp (*.*)|*.*||",
		this);

	dialog.m_ofn.lpstrTitle = L"Chọn ảnh hoặc video để gửi";

	if (dialog.DoModal() == IDOK)
		StartUpload(dialog.GetPathName());
}

void CMFCChatDlg::OnDropFiles(HDROP hDropInfo)
{
	const UINT count = ::DragQueryFile(hDropInfo, 0xFFFFFFFF, nullptr, 0);

	if (count > 0)
	{
		TCHAR path[MAX_PATH * 2] = {};

		if (::DragQueryFile(hDropInfo, 0, path, MAX_PATH * 2) > 0)
			StartUpload(path);
	}

	::DragFinish(hDropInfo);
}

void CMFCChatDlg::StartUpload(const CString& path)
{
	if (m_currentChatUser.IsEmpty())
	{
		AfxMessageBox(L"Hãy chọn một cuộc trò chuyện trước.");
		return;
	}

	if (m_uploadActive)
	{
		AfxMessageBox(L"Đang gửi một tệp khác, vui lòng chờ.");
		return;
	}

	if (!m_tcpClient.IsConnected())
	{
		AfxMessageBox(L"Not connected to TCP server.");
		return;
	}

	std::string kind;

	if (!ClassifyMediaFile(path, kind))
	{
		AfxMessageBox(
			L"Chỉ hỗ trợ ảnh (jpg, png, gif, bmp, webp) và "
			L"video (mp4, webm, mov, avi, mkv, wmv).");
		return;
	}

	int64_t size = 0;

	if (!GetFileSize64(path, size) || size <= 0)
	{
		AfxMessageBox(L"Không đọc được tệp này.");
		return;
	}

	const int64_t limit = (kind == "image") ? kMaxImageBytes : kMaxVideoBytes;

	if (size > limit)
	{
		CString message;
		message.Format(
			L"Tệp quá lớn. Giới hạn là %d MB cho %s.",
			static_cast<int>(limit / (1024 * 1024)),
			kind == "image" ? L"ảnh" : L"video");

		AfxMessageBox(message);
		return;
	}

	const int slash = path.ReverseFind(_T('\\'));

	m_uploadName = (slash >= 0) ? path.Mid(slash + 1) : path;
	m_uploadPath = path;
	m_uploadSize = size;

	// Ghi nhớ đích ngay lúc bấm gửi, kể cả khi người dùng đổi hội thoại sau đó.
	m_uploadIsGroup = m_isGroupChat;
	m_uploadGroupId = m_currentGroupId;
	m_uploadReceiver = m_isGroupChat ? std::string() : ToUtf8(m_currentChatUser);

	static int counter = 0;

	m_uploadId = "u" + std::to_string(::GetTickCount64()) + "_" +
		std::to_string(++counter);

	m_uploadCancel = false;
	m_uploadActive = true;

	// Ảnh: tạo bản thu nhỏ để người nhận xem ngay trong khung chat.
	// Không tạo được (vd định dạng webp) thì chỉ gửi dạng liên kết.
	m_uploadThumbJpeg.clear();

	if (kind == "image")
		PrepareThumbnailJpeg(path, m_uploadThumbJpeg);

	SetStatusText(CString(_T("Đang chuẩn bị gửi ")) + m_uploadName + _T("…"));

	if (!m_tcpClient.SendRaw(nlohmann::json{
		{"action", "UPLOAD_BEGIN"},
		{"uploadId", m_uploadId},
		{"fileName", ToUtf8(m_uploadName)},
		{"size", size}
		}.dump()))
	{
		FinishUpload();
		AfxMessageBox(L"Không gửi được yêu cầu tải lên.");
	}
}

void CMFCChatDlg::FinishUpload()
{
	m_uploadCancel = true;

	if (m_uploadThread.joinable())
		m_uploadThread.join();

	m_uploadActive = false;
	m_uploadId.clear();

	UpdateSubtitle();
}

// Server đã sẵn sàng nhận: chạy thread đọc file và gửi từng đoạn.
void CMFCChatDlg::OnUploadReady(const std::string& payload)
{
	using json = nlohmann::json;

	if (!m_uploadActive)
		return;

	try
	{
		json response = json::parse(payload);

		if (response.value("uploadId", std::string()) != m_uploadId)
			return;
	}
	catch (const json::exception&)
	{
		return;
	}

	if (m_uploadThread.joinable())
		m_uploadThread.join();

	const HWND hwnd = m_hWnd;
	const CString path = m_uploadPath;
	const std::string uploadId = m_uploadId;
	const int64_t size = m_uploadSize;

	m_uploadThread = std::thread(
		[this, hwnd, path, uploadId, size]()
		{
			UploadWorker(hwnd, path, uploadId, size);
		});
}

// Chạy ở thread riêng: đọc file, mã hóa Base64 và gửi từng đoạn.
void CMFCChatDlg::UploadWorker(
	HWND hwnd,
	CString path,
	std::string uploadId,
	int64_t size)
{
	FILE* file = nullptr;

	if (_wfopen_s(&file, path, L"rb") != 0 || file == nullptr)
	{
		::PostMessage(hwnd, WM_APP_UPLOAD_PROGRESS, 0, 1);
		return;
	}

	std::vector<unsigned char> buffer(kUploadChunkBytes);
	int64_t sent = 0;
	bool ok = true;

	while (sent < size)
	{
		if (m_uploadCancel.load())
		{
			ok = false;
			break;
		}

		const size_t count = fread(buffer.data(), 1, buffer.size(), file);

		if (count == 0)
		{
			ok = false;
			break;
		}

		const nlohmann::json chunk = {
			{"action", "UPLOAD_CHUNK"},
			{"uploadId", uploadId},
			{"offset", sent},
			{"data", b64::Encode(buffer.data(), count)}
		};

		if (!m_tcpClient.SendRaw(chunk.dump()))
		{
			ok = false;
			break;
		}

		sent += static_cast<int64_t>(count);

		::PostMessage(
			hwnd, WM_APP_UPLOAD_PROGRESS,
			static_cast<WPARAM>(sent * 100 / size), 0);
	}

	fclose(file);

	if (ok)
	{
		ok = m_tcpClient.SendRaw(nlohmann::json{
			{"action", "UPLOAD_END"},
			{"uploadId", uploadId}
			}.dump());
	}

	// Bị hủy chủ động (đóng cửa sổ hoặc server từ chối) thì không báo lỗi thêm.
	if (!ok && !m_uploadCancel.load())
		::PostMessage(hwnd, WM_APP_UPLOAD_PROGRESS, 0, 1);
}

LRESULT CMFCChatDlg::OnUploadProgress(WPARAM wParam, LPARAM lParam)
{
	if (!m_uploadActive)
		return 0;

	if (lParam == 1)
	{
		FinishUpload();
		AfxMessageBox(L"Gửi tệp thất bại. Vui lòng thử lại.");
		return 0;
	}

	CString status;
	status.Format(
		L"Đang gửi %s… %d%%",
		static_cast<LPCTSTR>(m_uploadName),
		static_cast<int>(wParam));

	SetStatusText(status);
	return 0;
}

// Server đã lưu tệp: gửi tin nhắn tham chiếu tới tệp đó.
void CMFCChatDlg::OnUploadResult(const std::string& payload)
{
	using json = nlohmann::json;

	if (!m_uploadActive)
		return;

	bool success = false;
	int64_t attachmentId = 0;
	std::string errorText = "Unknown error";

	try
	{
		json response = json::parse(payload);

		success = response.value("success", false);
		attachmentId = response.value("attachmentId", int64_t(0));

		if (!success)
		{
			errorText = response.value(
				"message", std::string("Unknown error"));
		}
	}
	catch (const json::exception&)
	{
		errorText = "Invalid response from server";
	}

	const bool isGroup = m_uploadIsGroup;
	const int64_t groupId = m_uploadGroupId;
	const std::string receiver = m_uploadReceiver;

	const std::string thumbJpeg = m_uploadThumbJpeg;
	m_uploadThumbJpeg.clear();

	FinishUpload();

	if (!success || attachmentId <= 0)
	{
		CString message;
		message.Format(
			L"Gửi tệp thất bại: %s",
			static_cast<LPCTSTR>(FromUtf8(errorText)));

		AfxMessageBox(message);
		return;
	}

	// Gửi ảnh thu nhỏ trước tin nhắn: server xử lý tuần tự theo thứ tự gửi nên
	// tin nhắn sẽ được đánh dấu "có ảnh thu nhỏ".
	if (!thumbJpeg.empty())
	{
		m_tcpClient.SendRaw(json{
			{"action", "SET_THUMBNAIL"},
			{"attachmentId", attachmentId},
			{"data", b64::Encode(thumbJpeg)}
			}.dump());

		// Người gửi dùng luôn bản vừa tạo, không cần tải lại từ server.
		ChatThumbnail thumbnail;

		if (DecodeThumbnail(thumbJpeg, thumbnail))
		{
			auto old = m_thumbs.find(attachmentId);

			if (old != m_thumbs.end() && old->second.bitmap != nullptr)
				::DeleteObject(old->second.bitmap);

			m_thumbs[attachmentId] = thumbnail;
			m_thumbRequested.insert(attachmentId);
		}
	}

	json request;

	if (isGroup)
	{
		request = {
			{"action", "SEND_GROUP_MESSAGE"},
			{"groupId", groupId},
			{"attachmentId", attachmentId}
		};
	}
	else
	{
		request = {
			{"action", "SEND_MESSAGE"},
			{"recipient", receiver},
			{"attachmentId", attachmentId}
		};
	}

	if (!m_tcpClient.SendRaw(request.dump()))
		AfxMessageBox(L"Không gửi được tin nhắn chứa tệp.");
}

// ------------------------------------------------------------- Tải / mở tệp

void CMFCChatDlg::OnRichEditLink(NMHDR* pNMHDR, LRESULT* pResult)
{
	ENLINK* link = reinterpret_cast<ENLINK*>(pNMHDR);

	*pResult = 0;

	if (link == nullptr || link->msg != WM_LBUTTONUP)
		return;

	for (const ChatAttachmentLink& item : m_attachmentLinks)
	{
		if (link->chrg.cpMin >= item.start && link->chrg.cpMin < item.end)
		{
			OpenAttachment(item);
			break;
		}
	}
}

void CMFCChatDlg::OpenAttachment(const ChatAttachmentLink& link)
{
	CString finalPath;
	finalPath.Format(
		L"%s\\%lld_%s",
		static_cast<LPCTSTR>(CacheDirectory()),
		static_cast<long long>(link.id),
		static_cast<LPCTSTR>(SafeFileName(FromUtf8(link.name))));

	// Đã tải trước đó: mở luôn.
	int64_t existingSize = 0;

	if (GetFileSize64(finalPath, existingSize) && existingSize == link.size)
	{
		::ShellExecute(
			m_hWnd, _T("open"), finalPath, nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	if (m_downloadActive)
	{
		AfxMessageBox(L"Đang tải một tệp khác, vui lòng chờ.");
		return;
	}

	if (!m_tcpClient.IsConnected())
	{
		AfxMessageBox(L"Not connected to TCP server.");
		return;
	}

	const CString partPath = finalPath + _T(".part");

	FILE* file = nullptr;

	if (_wfopen_s(&file, partPath, L"wb") != 0 || file == nullptr)
	{
		AfxMessageBox(L"Không tạo được tệp tạm để tải về.");
		return;
	}

	m_downloadFile = file;
	m_downloadActive = true;
	m_downloadId = link.id;
	m_downloadSize = link.size;
	m_downloadOffset = 0;
	m_downloadPartPath = partPath;
	m_downloadFinalPath = finalPath;
	m_downloadName = FromUtf8(link.name);

	SetStatusText(CString(_T("Đang tải ")) + m_downloadName + _T("…"));

	if (!m_tcpClient.SendRaw(nlohmann::json{
		{"action", "GET_ATTACHMENT"},
		{"attachmentId", link.id},
		{"offset", int64_t(0)}
		}.dump()))
	{
		AbortDownload();
		AfxMessageBox(L"Không gửi được yêu cầu tải tệp.");
	}
}

void CMFCChatDlg::AbortDownload()
{
	if (m_downloadFile != nullptr)
	{
		fclose(m_downloadFile);
		m_downloadFile = nullptr;
	}

	if (!m_downloadPartPath.IsEmpty())
		::DeleteFile(m_downloadPartPath);

	m_downloadPartPath.Empty();
	m_downloadActive = false;

	UpdateSubtitle();
}

void CMFCChatDlg::OnAttachmentChunk(const std::string& payload)
{
	using json = nlohmann::json;

	if (!m_downloadActive)
		return;

	std::string errorText;
	std::string raw;
	bool eof = false;
	int64_t totalSize = 0;

	try
	{
		json response = json::parse(payload);

		if (response.value("attachmentId", int64_t(0)) != m_downloadId)
			return;   // gói của lần tải khác

		if (!response.value("success", false))
		{
			errorText = response.value(
				"message", std::string("Unknown error"));
		}
		else if (response.value("offset", int64_t(-1)) != m_downloadOffset)
		{
			errorText = "Unexpected data offset";
		}
		else if (!b64::Decode(response.value("data", std::string()), raw))
		{
			errorText = "Invalid data";
		}
		else
		{
			eof = response.value("eof", false);
			totalSize = response.value("size", int64_t(0));
		}
	}
	catch (const json::exception&)
	{
		errorText = "Invalid response from server";
	}

	if (errorText.empty() && !raw.empty() &&
		fwrite(raw.data(), 1, raw.size(), m_downloadFile) != raw.size())
	{
		errorText = "Cannot write file";
	}

	if (!errorText.empty())
	{
		AbortDownload();

		CString message;
		message.Format(
			L"Tải tệp thất bại: %s",
			static_cast<LPCTSTR>(FromUtf8(errorText)));

		AfxMessageBox(message);
		return;
	}

	m_downloadOffset += static_cast<int64_t>(raw.size());

	if (eof || raw.empty())
	{
		fclose(m_downloadFile);
		m_downloadFile = nullptr;

		if (m_downloadOffset != totalSize ||
			!::MoveFileEx(
				m_downloadPartPath, m_downloadFinalPath,
				MOVEFILE_REPLACE_EXISTING))
		{
			AbortDownload();
			AfxMessageBox(L"Tải tệp thất bại: dữ liệu không đầy đủ.");
			return;
		}

		const CString finalPath = m_downloadFinalPath;

		m_downloadPartPath.Empty();
		m_downloadActive = false;
		UpdateSubtitle();

		::ShellExecute(
			m_hWnd, _T("open"), finalPath, nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const int percent = (m_downloadSize > 0)
		? static_cast<int>(m_downloadOffset * 100 / m_downloadSize)
		: 0;

	CString status;
	status.Format(
		L"Đang tải %s… %d%%",
		static_cast<LPCTSTR>(m_downloadName),
		percent);

	SetStatusText(status);

	if (!m_tcpClient.SendRaw(nlohmann::json{
		{"action", "GET_ATTACHMENT"},
		{"attachmentId", m_downloadId},
		{"offset", m_downloadOffset}
		}.dump()))
	{
		AbortDownload();
		AfxMessageBox(L"Mất kết nối khi đang tải tệp.");
	}
}

void CMFCChatDlg::OnDestroy()
{
	// Hủy upload đang chạy; ngắt kết nối để thread đang chờ send() thoát ra.
	m_uploadCancel = true;
	m_tcpClient.Disconnect();

	if (m_uploadThread.joinable())
		m_uploadThread.join();

	AbortDownload();

	// Xóa nội dung để RichEdit giải phóng các đối tượng ảnh OLE.
	if (CRichEditCtrl* edit =
		static_cast<CRichEditCtrl*>(GetDlgItem(IDC_RICHEDIT_MESSAGE)))
	{
		edit->SetWindowText(_T(""));
		edit->SetOLECallback(nullptr);
	}

	if (m_oleCallback != nullptr)
	{
		m_oleCallback->Release();
		m_oleCallback = nullptr;
	}

	for (auto& entry : m_thumbs)
	{
		if (entry.second.bitmap != nullptr)
			::DeleteObject(entry.second.bitmap);
	}

	m_thumbs.clear();

	// Phải giải phóng bitmap trước khi tắt GDI+.
	m_avatarBitmaps.clear();

	if (m_gdiplusToken != 0)
	{
		Gdiplus::GdiplusShutdown(m_gdiplusToken);
		m_gdiplusToken = 0;
	}

	CDialogEx::OnDestroy();
}

// ----------------------------------------------- Ảnh thu nhỏ trong khung chat

void CMFCChatDlg::RequestThumbnails(const std::set<int64_t>& ids)
{
	if (ids.empty() || !m_tcpClient.IsConnected())
		return;

	for (int64_t id : ids)
	{
		// Mỗi ảnh chỉ xin một lần (kể cả khi lỗi) để không bị lặp vô hạn.
		if (m_thumbs.count(id) != 0 || m_thumbRequested.count(id) != 0)
			continue;

		m_thumbRequested.insert(id);

		if (m_tcpClient.SendRaw(nlohmann::json{
			{"action", "GET_THUMBNAIL"},
			{"attachmentId", id}
			}.dump()))
		{
			++m_thumbPending;
		}
	}
}

void CMFCChatDlg::OnThumbnailResult(const std::string& payload)
{
	using json = nlohmann::json;

	if (m_thumbPending > 0)
		--m_thumbPending;

	try
	{
		json response = json::parse(payload);

		if (response.value("success", false))
		{
			const int64_t id = response.value("attachmentId", int64_t(0));

			std::string jpeg;
			ChatThumbnail thumbnail;

			if (id > 0 &&
				b64::Decode(response.value("data", std::string()), jpeg) &&
				DecodeThumbnail(jpeg, thumbnail))
			{
				auto old = m_thumbs.find(id);

				if (old != m_thumbs.end() && old->second.bitmap != nullptr)
					::DeleteObject(old->second.bitmap);

				m_thumbs[id] = thumbnail;
				m_thumbDirty = true;
			}
		}
	}
	catch (const json::exception&)
	{
	}

	// Chờ đủ các ảnh đang xin rồi mới vẽ lại một lần.
	if (m_thumbPending == 0 && m_thumbDirty)
	{
		m_thumbDirty = false;
		ReRenderLastHistory();
	}
}

// Vẽ lại lịch sử đang hiển thị từ dữ liệu đã có, không hỏi lại server.
void CMFCChatDlg::ReRenderLastHistory()
{
	if (m_lastHistoryPayload.empty())
		return;

	const std::string payload = m_lastHistoryPayload;

	if (m_lastHistoryWasGroup)
		DisplayGroupHistoryResponse(payload);
	else
		DisplayHistoryResponse(payload);
}

// Bấm vào ảnh thu nhỏ để mở ảnh gốc; chặn nhấp đúp (sẽ mở Paint).
void CMFCChatDlg::OnRichEditMsgFilter(NMHDR* pNMHDR, LRESULT* pResult)
{
	MSGFILTER* filter = reinterpret_cast<MSGFILTER*>(pNMHDR);

	*pResult = 0;

	if (filter == nullptr ||
		(filter->msg != WM_LBUTTONUP && filter->msg != WM_LBUTTONDBLCLK))
	{
		return;
	}

	CRichEditCtrl* edit =
		static_cast<CRichEditCtrl*>(GetDlgItem(IDC_RICHEDIT_MESSAGE));

	if (edit == nullptr)
		return;

	const CPoint point(
		static_cast<short>(LOWORD(filter->lParam)),
		static_cast<short>(HIWORD(filter->lParam)));

	for (const ChatAttachmentLink& item : m_attachmentLinks)
	{
		if (item.imagePos < 0 || item.imageWidth <= 0)
			continue;

		const CPoint topLeft = edit->GetCharPos(item.imagePos);

		const CRect imageRect(
			topLeft.x,
			topLeft.y - 4,
			topLeft.x + item.imageWidth + 4,
			topLeft.y + item.imageHeight + 8);

		if (!imageRect.PtInRect(point))
			continue;

		if (filter->msg == WM_LBUTTONDBLCLK)
			*pResult = 1;   // không cho RichEdit mở đối tượng ảnh bằng Paint
		else
			OpenAttachment(item);

		return;
	}
}