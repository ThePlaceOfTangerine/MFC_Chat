
// Week2Dlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "Week2.h"
#include "Week2Dlg.h"
#include "afxdialogex.h"	
#include "CLoginDlg.h"
#include <richedit.h>

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

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CWeek2Dlg dialog



CWeek2Dlg::CWeek2Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WEEK2_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CWeek2Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CWeek2Dlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BTN_SEND, &CWeek2Dlg::OnBnClickedBtnSend)
	ON_NOTIFY(TVN_SELCHANGED, IDC_TREE_FRIENDS, &CWeek2Dlg::OnTvnSelchangedTreeFriends)
END_MESSAGE_MAP()


// CWeek2Dlg message handlers

BOOL CWeek2Dlg::OnInitDialog()
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
	InitTree();
	InitFakeData();

	HWND hTree = ::GetDlgItem(m_hWnd, IDC_TREE_FRIENDS);
	HTREEITEM hFirst = TreeView_GetRoot(hTree);
	if (hFirst) TreeView_SelectItem(hTree, hFirst);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CWeek2Dlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CDialogEx::OnSysCommand(nID, lParam);
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CWeek2Dlg::OnPaint()
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
HCURSOR CWeek2Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

HTREEITEM CWeek2Dlg::AddTreeItem(HWND hTree, HTREEITEM hParent, LPCTSTR text)
{
	TVINSERTSTRUCT tvis = { 0 };
	tvis.hParent = hParent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT;
	tvis.item.pszText = (LPTSTR)text;
	return TreeView_InsertItem(hTree, &tvis);
}

void CWeek2Dlg::InitTree()
{
	HWND hTree = ::GetDlgItem(m_hWnd, IDC_TREE_FRIENDS);

	AddTreeItem(hTree, TVI_ROOT, _T("Nguyen Van A"));
	AddTreeItem(hTree, TVI_ROOT, _T("Tran Thi B"));
	AddTreeItem(hTree, TVI_ROOT, _T("Le Van C"));
	AddTreeItem(hTree, TVI_ROOT, _T("Pham Thi D"));
}

void CWeek2Dlg::AppendMessage(BOOL bMe, LPCTSTR name, LPCTSTR text)
{
	HWND hRich = ::GetDlgItem(m_hWnd, IDC_RICHEDIT_MSG);

	// 1. Đưa con trỏ về cuối nội dung
	int len = ::GetWindowTextLength(hRich);
	::SendMessage(hRich, EM_SETSEL, len, len);

	// 2. Canh lề đoạn: mình bên phải, người kia bên trái
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

	CString body = CString(text) + _T("\r\r");   // \r thừa để cách dòng
	::SendMessage(hRich, EM_REPLACESEL, FALSE, (LPARAM)(LPCTSTR)body);

	// 5. Cuộn xuống tin mới nhất
	::SendMessage(hRich, WM_VSCROLL, SB_BOTTOM, 0);
}

void CWeek2Dlg::InitFakeData()
{
	m_chats[_T("Nguyen Van A")] = {
		{ _T("09:00"), _T("Nguyen Van A"), _T("Chao ban!") },
		{ _T("09:01"), m_strUser,          _T("Chao A, khoe khong?") },
		{ _T("09:02"), _T("Nguyen Van A"), _T("Minh khoe, cam on") }
	};
	m_chats[_T("Tran Thi B")] = {
		{ _T("10:15"), _T("Tran Thi B"), _T("Toi nay di an khong?") },
		{ _T("10:16"), m_strUser,        _T("Di chu, may gio?") }
	};
	m_chats[_T("Le Van C")] = {
		{ _T("14:00"), _T("Le Van C"), _T("Gui minh bao cao nhe") }
	};
	m_chats[_T("Pham Thi D")] = {
		{ _T("16:30"), _T("Pham Thi D"), _T("Hop luc 9h sang mai") }
	};
}

void CWeek2Dlg::OnTvnSelchangedTreeFriends(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	*pResult = 0;

	HWND hTree = ::GetDlgItem(m_hWnd, IDC_TREE_FRIENDS);
	HTREEITEM hItem = pNMTreeView->itemNew.hItem;

	TCHAR name[128] = { 0 };
	TVITEM tv = { 0 };
	tv.mask = TVIF_TEXT;
	tv.hItem = hItem;
	tv.pszText = name;
	tv.cchTextMax = 128;
	TreeView_GetItem(hTree, &tv);

	m_strCurrent = name;
	ShowChat();
}

void CWeek2Dlg::ShowChat()
{
	::SetDlgItemText(m_hWnd, IDC_RICHEDIT_MSG, _T(""));   // xóa sạch

	for (const ChatMsg& m : m_chats[m_strCurrent])
	{
		BOOL bMe = (m.sender == m_strUser);
		AppendMessage(bMe, bMe ? _T("Ban") : (LPCTSTR)m.sender, m.text);
	}

	::SetWindowText(m_hWnd, CString(_T("Chat voi ")) + m_strCurrent);
}

void CWeek2Dlg::AddMessage(LPCTSTR time, LPCTSTR sender, LPCTSTR msg)
{
	HWND hList = ::GetDlgItem(m_hWnd, IDC_RICHEDIT_MSG);

	LVITEM item = { 0 };
	item.mask = LVIF_TEXT;
	item.iItem = ListView_GetItemCount(hList);
	item.pszText = (LPTSTR)msg;
	int idx = ListView_InsertItem(hList, &item);

	ListView_EnsureVisible(hList, idx, FALSE);
}

void CWeek2Dlg::OnBnClickedBtnSend()
{
	if (m_strCurrent.IsEmpty())
	{
		::MessageBox(m_hWnd, _T("Hay chon nguoi de chat"), _T("Thong bao"), MB_OK);
		return;
	}

	TCHAR msg[256] = { 0 };
	::GetDlgItemText(m_hWnd, IDC_EDIT_MSG, msg, 256);
	if (msg[0] == 0) return;

	SYSTEMTIME st;
	::GetLocalTime(&st);
	TCHAR szTime[16];
	wsprintf(szTime, _T("%02d:%02d"), st.wHour, st.wMinute);

	m_chats[m_strCurrent].push_back({ szTime, m_strUser, msg });   // lưu lại
	AddMessage(szTime, m_strUser, msg);                            // hiện lên list
	::SetDlgItemText(m_hWnd, IDC_EDIT_MSG, _T(""));
}
