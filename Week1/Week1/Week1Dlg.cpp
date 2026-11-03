
// Week1Dlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "Week1.h"
#include "Week1Dlg.h"
#include "afxdialogex.h"

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


// CWeek1Dlg dialog

CWeek1Dlg::CWeek1Dlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_WEEK1_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CWeek1Dlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CWeek1Dlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BTN_SAVE, &CWeek1Dlg::OnBnClickedBtnSave)
	ON_BN_CLICKED(IDC_BTN_LOAD, &CWeek1Dlg::OnBnClickedBtnLoad)
END_MESSAGE_MAP()


// CWeek1Dlg message handlers

BOOL CWeek1Dlg::OnInitDialog()
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

void CWeek1Dlg::OnSysCommand(UINT nID, LPARAM lParam)
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

void CWeek1Dlg::OnPaint()
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
HCURSOR CWeek1Dlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CWeek1Dlg::OnBnClickedBtnSave()
{
	// Chưa có đường dẫn -> hiện hộp thoại Save As
	if (m_strFilePath.IsEmpty())
	{
		CFileDialog dlg(
			FALSE,
			_T("txt"),
			_T("data.txt"),
			OFN_OVERWRITEPROMPT,
			_T("Text Files (*.txt)|*.txt|All Files (*.*)|*.*||")
		);

		if (dlg.DoModal() != IDOK)
			return;   // bấm Cancel thì không lưu

		m_strFilePath = dlg.GetPathName();
		::SetDlgItemText(m_hWnd, IDC_STATIC_PATH, m_strFilePath);
	}

	// Lấy nội dung từ edit box
	HWND hEdit = ::GetDlgItem(m_hWnd, IDC_EDIT_CONTENT);
	int len = ::GetWindowTextLengthA(hEdit);
	std::string text(len + 1, '\0');
	::GetWindowTextA(hEdit, &text[0], len + 1);
	text.resize(len);

	// Ghi file
	HANDLE hFile = ::CreateFile(m_strFilePath, GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		AfxMessageBox(_T("Cannot save file."));
		return;
	}

	DWORD written = 0;
	::WriteFile(hFile, text.data(), (DWORD)text.size(), &written, nullptr);
	::CloseHandle(hFile);
}

void CWeek1Dlg::OnBnClickedBtnLoad()
{
	CFileDialog dlg(
		TRUE,
		_T("txt"),
		nullptr,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("Text Files (*.txt)|*.txt|All Files (*.*)|*.*||")
	);

	if (dlg.DoModal() != IDOK)
		return;

	m_strFilePath = dlg.GetPathName();
	::SetDlgItemText(m_hWnd, IDC_STATIC_PATH, m_strFilePath);   // cập nhật static text

	HANDLE hFile = ::CreateFile(
		m_strFilePath,
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		AfxMessageBox(_T("Cannot open file."));
		return;
	}

	DWORD fileSize = ::GetFileSize(hFile, nullptr);
	if (fileSize == INVALID_FILE_SIZE)
	{
		::CloseHandle(hFile);
		AfxMessageBox(_T("Cannot get file size."));
		return;
	}

	std::string buffer(fileSize, '\0');
	DWORD bytesRead = 0;
	::ReadFile(hFile, &buffer[0], fileSize, &bytesRead, nullptr);
	::CloseHandle(hFile);
	buffer.resize(bytesRead);

	::SetDlgItemTextA(m_hWnd, IDC_EDIT_CONTENT, buffer.c_str());
}
