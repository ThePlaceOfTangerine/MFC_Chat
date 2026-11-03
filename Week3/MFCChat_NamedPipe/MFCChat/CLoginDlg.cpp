// CLoginDlg.cpp : implementation file
//

#include "pch.h"
#include "MFCChat.h"
#include "CLoginDlg.h"
#include "afxdialogex.h"
#include "CRegisterDlg.h"
#include "Database.h"
#include <atlconv.h>
#include <string>

// CLoginDlg dialog

IMPLEMENT_DYNAMIC(CLoginDlg, CDialogEx)

CLoginDlg::CLoginDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_LOGIN_DIALOG, pParent)
	, m_strUsername(_T(""))
	, m_strPassword(_T(""))
{

}

CLoginDlg::~CLoginDlg()
{
}

void CLoginDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_LOGIN_USERNAME, m_strUsername);
	DDX_Text(pDX, IDC_LOGIN_PASSWORD, m_strPassword);
}


BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
	ON_BN_CLICKED(IDC_BUTTON_REGISTER, &CLoginDlg::OnBnClickedButtonRegister)
	ON_BN_CLICKED(IDC_BUTTON_LOGIN, &CLoginDlg::OnBnClickedButtonLogin)
END_MESSAGE_MAP()


// CLoginDlg message handlers

void CLoginDlg::OnBnClickedButtonLogin()
{
	// TODO: Add your control notification handler code here\

	if (!UpdateData(TRUE))
		return;

	if (m_strUsername.IsEmpty() || m_strPassword.IsEmpty())
	{
		SetDlgItemText(IDC_LOGIN_ERROR, _T("Please fill in all fields."));
		return;
	}

	Database db;

	if (!db.Open("D:\\Work\\CMC\\MFCChat_NamedPipe\\chat.db"))
	{
		AfxMessageBox(_T("Cannot open database."));
		return;
	}

	if (!db.Initialize())
	{
		db.Close();
		AfxMessageBox(_T("Cannot initialize database."));
		return;
	}

	CT2A username(m_strUsername, CP_UTF8);
	CT2A password(m_strPassword, CP_UTF8);

	int userId = 0;
	std::string displayName;

	bool authenticated = db.AuthenticateUser(username, password, userId, displayName);

	db.Close();

	if (authenticated)
	{
		EndDialog(IDOK);
		return;
	}

	SetDlgItemText(IDC_LOGIN_ERROR, _T("Invalid username or password."));
}


void CLoginDlg::OnBnClickedButtonRegister()
{
	// TODO: Add your control notification handler code here

	CRegisterDlg dlg(this);
	dlg.DoModal();
}

CString CLoginDlg::GetUsername() const
{
	return m_strUsername;
}