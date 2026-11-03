
// Week4Dlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "Week4.h"
#include "Week4Dlg.h"
#include "afxdialogex.h"
#include <vector>
#include <afxsock.h>      
#include <ws2tcpip.h>     
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

static bool SendAll(SOCKET s, const char* buf, int len)
{
	while (len > 0)
	{
		int n = ::send(s, buf, len, 0);
		if (n <= 0) return false;
		buf += n;
		len -= n;
	}
	return true;
}

static bool RecvAll(SOCKET s, char* buf, int len)
{
	while (len > 0)
	{
		int n = ::recv(s, buf, len, 0);
		if (n <= 0) return false;      // 0 = đối phương đóng, <0 = lỗi
		buf += n;
		len -= n;
	}
	return true;
}

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


// CWeek4Dlg dialog



CWeek4Dlg::CWeek4Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WEEK4_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CWeek4Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CWeek4Dlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BTN_LISTEN, &CWeek4Dlg::OnBnClickedBtnListen)
	ON_BN_CLICKED(IDC_BTN_CONNECT, &CWeek4Dlg::OnBnClickedBtnConnect)
	ON_BN_CLICKED(IDC_BTN_DISCONNECT, &CWeek4Dlg::OnBnClickedBtnDisconnect)
	ON_BN_CLICKED(IDC_BTN_SEND, &CWeek4Dlg::OnBnClickedBtnSend)
	ON_MESSAGE(WM_NET_CONNECTED, &CWeek4Dlg::OnNetConnected)
	ON_MESSAGE(WM_NET_MESSAGE, &CWeek4Dlg::OnNetMessage)
	ON_MESSAGE(WM_NET_CLOSED, &CWeek4Dlg::OnNetClosed)
	ON_MESSAGE(WM_NET_ERROR, &CWeek4Dlg::OnNetError)
END_MESSAGE_MAP()


// CWeek4Dlg message handlers

BOOL CWeek4Dlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

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

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CWeek4Dlg::OnSysCommand(UINT nID, LPARAM lParam)
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

void CWeek4Dlg::OnPaint()
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
HCURSOR CWeek4Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CWeek4Dlg::StartNet(bool server)
{
	if (m_sock != INVALID_SOCKET || m_listen != INVALID_SOCKET) return;

	TCHAR ip[64] = {}, port[16] = {};
	::GetDlgItemText(m_hWnd, IDC_EDIT_IP, ip, 64);
	::GetDlgItemText(m_hWnd, IDC_EDIT_PORT, port, 16);

	NetParam* np = new NetParam{ this, server, ip, _ttoi(port) };
	::SetDlgItemText(m_hWnd, IDC_STATIC_STATUS,
		server ? _T("Dang cho ket noi...") : _T("Dang ket noi..."));
	AfxBeginThread(NetThread, np);
}

void CWeek4Dlg::OnBnClickedBtnListen() { StartNet(true); }
void CWeek4Dlg::OnBnClickedBtnConnect() { StartNet(false); }

UINT CWeek4Dlg::NetThread(LPVOID pParam)
{
	NetParam* np = (NetParam*)pParam;
	CWeek4Dlg* dlg = np->dlg;
	HWND hWnd = dlg->m_hWnd;
	SOCKET s = INVALID_SOCKET;

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((u_short)np->port);

	if (np->server)
	{
		SOCKET ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if (::bind(ls, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR ||
			::listen(ls, 1) == SOCKET_ERROR)
		{
			::PostMessage(hWnd, WM_NET_ERROR, ::WSAGetLastError(), 0);
			::closesocket(ls);
			delete np;
			return 0;
		}
		dlg->m_listen = ls;
		s = ::accept(ls, NULL, NULL);       // chặn tới khi có người nối vào
		dlg->m_listen = INVALID_SOCKET;
		::closesocket(ls);
	}
	else
	{
		s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (::inet_pton(AF_INET, CT2A(np->ip), &addr.sin_addr) != 1 ||
			::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			::PostMessage(hWnd, WM_NET_ERROR, ::WSAGetLastError(), 0);
			::closesocket(s);
			delete np;
			return 0;
		}
	}
	delete np;

	if (s == INVALID_SOCKET)       // bị Disconnect khi đang chờ accept
	{
		::PostMessage(hWnd, WM_NET_CLOSED, 0, 0);
		return 0;
	}

	::PostMessage(hWnd, WM_NET_CONNECTED, (WPARAM)s, 0);

	// Vòng lặp nhận: [4 byte độ dài][nội dung]
	while (true)
	{
		u_long netLen = 0;
		if (!RecvAll(s, (char*)&netLen, 4)) break;

		int len = (int)::ntohl(netLen);
		if (len < 0 || len > 10 * 1024 * 1024 || (len % sizeof(TCHAR)) != 0)
			break;                           // dữ liệu bất thường -> ngắt

		std::vector<char> buf(len);
		if (len > 0 && !RecvAll(s, buf.data(), len)) break;

		CString* pText = new CString;
		pText->SetString((LPCTSTR)buf.data(), len / sizeof(TCHAR));
		::PostMessage(hWnd, WM_NET_MESSAGE, 0, (LPARAM)pText);
	}

	::closesocket(s);
	::PostMessage(hWnd, WM_NET_CLOSED, 0, 0);
	return 0;
}

LRESULT CWeek4Dlg::OnNetConnected(WPARAM wParam, LPARAM)
{
	m_sock = (SOCKET)wParam;
	::SetDlgItemText(m_hWnd, IDC_STATIC_STATUS, _T("Da ket noi"));
	return 0;
}

LRESULT CWeek4Dlg::OnNetMessage(WPARAM, LPARAM lParam)
{
	CString* pText = (CString*)lParam;
	AppendMessage(FALSE, _T("Doi phuong"), *pText);
	delete pText;                         // thread đã new, UI phải delete
	return 0;
}

LRESULT CWeek4Dlg::OnNetClosed(WPARAM, LPARAM)
{
	m_sock = INVALID_SOCKET;
	::SetDlgItemText(m_hWnd, IDC_STATIC_STATUS, _T("Chua ket noi"));
	return 0;
}

LRESULT CWeek4Dlg::OnNetError(WPARAM wParam, LPARAM)
{
	CString s;
	s.Format(_T("Loi mang (WSA error %d)"), (int)wParam);
	::SetDlgItemText(m_hWnd, IDC_STATIC_STATUS, s);
	return 0;
}

void CWeek4Dlg::OnBnClickedBtnSend()
{
	if (m_sock == INVALID_SOCKET)
	{
		::MessageBox(m_hWnd, _T("Chua ket noi"), _T("Thong bao"), MB_OK);
		return;
	}

	CString text;
	GetDlgItemText(IDC_EDIT_MSG, text);
	if (text.IsEmpty()) return;

	int bytes = text.GetLength() * sizeof(TCHAR);
	u_long netLen = ::htonl(bytes);

	if (!SendAll(m_sock, (const char*)&netLen, 4) ||
		!SendAll(m_sock, (const char*)(LPCTSTR)text, bytes))
	{
		::MessageBox(m_hWnd, _T("Gui that bai"), _T("Loi"), MB_OK);
		return;
	}

	AppendMessage(TRUE, _T("Ban"), text);
	::SetDlgItemText(m_hWnd, IDC_EDIT_MSG, _T(""));
}

void CWeek4Dlg::Disconnect()
{
	if (m_listen != INVALID_SOCKET) ::closesocket(m_listen);     // hủy accept đang chờ
	if (m_sock != INVALID_SOCKET) ::shutdown(m_sock, SD_BOTH); // hủy recv đang chờ
}

void CWeek4Dlg::OnBnClickedBtnDisconnect() { Disconnect(); }

void CWeek4Dlg::OnDestroy()
{
	Disconnect();
	CDialogEx::OnDestroy();
}

void CWeek4Dlg::AppendMessage(BOOL bMe, LPCTSTR name, LPCTSTR text)
{
	HWND hRich = ::GetDlgItem(m_hWnd, IDC_RICHEDIT_MSG);

	// 1. Đưa con trỏ về cuối nội dung
	int len = ::GetWindowTextLength(hRich);
	::SendMessage(hRich, EM_SETSEL, len, len);

	// 2. Canh lề: mình bên phải, người kia bên trái
	PARAFORMAT2 pf = {};
	pf.cbSize = sizeof(pf);
	pf.dwMask = PFM_ALIGNMENT;
	pf.wAlignment = bMe ? PFA_RIGHT : PFA_LEFT;
	::SendMessage(hRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);

	// 3. Dòng tên: đậm, xanh (mình) hoặc đỏ (người kia)
	CHARFORMAT2 cf = {};
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR | CFM_BOLD;
	cf.dwEffects = CFE_BOLD;
	cf.crTextColor = bMe ? RGB(0, 0, 200) : RGB(220, 0, 0);
	::SendMessage(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

	CString line = CString(name) + _T("\r");
	::SendMessage(hRich, EM_REPLACESEL, FALSE, (LPARAM)(LPCTSTR)line);

	// 4. Dòng nội dung: thường, màu đen
	cf.dwEffects = 0;
	cf.crTextColor = RGB(0, 0, 0);
	::SendMessage(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

	CString body = CString(text) + _T("\r\r");
	::SendMessage(hRich, EM_REPLACESEL, FALSE, (LPARAM)(LPCTSTR)body);

	// 5. Cuộn xuống tin mới nhất
	::SendMessage(hRich, WM_VSCROLL, SB_BOTTOM, 0);
}