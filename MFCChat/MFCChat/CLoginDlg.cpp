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
#include "AutoRun.h"
#include "TcpChatClient.h"

using namespace std;

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

BOOL CLoginDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	m_checkAutoRun.SetCheck(
		IsAutoRunEnabled() ? BST_CHECKED : BST_UNCHECKED
	);

	return TRUE;
}

void CLoginDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_LOGIN_USERNAME, m_strUsername);
	DDX_Text(pDX, IDC_LOGIN_PASSWORD, m_strPassword);
	DDX_Control(pDX, IDC_CHECK_AUTORUN, m_checkAutoRun);
}


BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
	ON_BN_CLICKED(IDC_BUTTON_REGISTER, &CLoginDlg::OnBnClickedButtonRegister)
	ON_BN_CLICKED(IDC_BUTTON_LOGIN, &CLoginDlg::OnBnClickedButtonLogin)
END_MESSAGE_MAP()


// CLoginDlg message handlers

void CLoginDlg::OnBnClickedButtonLogin()
{
    if (!UpdateData(TRUE))
        return;

    m_strUsername.Trim();

    if (m_strUsername.IsEmpty() || m_strPassword.IsEmpty())
    {
        SetDlgItemText(
            IDC_LOGIN_ERROR,
            _T("Please fill in all fields."));
        return;
    }

    CT2A usernameUtf8(m_strUsername, CP_UTF8);
    CT2A passwordUtf8(m_strPassword, CP_UTF8);

    TcpChatClient client;

    if (!client.Connect("127.0.0.1", 5000))
    {
        SetDlgItemText(
            IDC_LOGIN_ERROR,
            _T("Cannot connect to TCP server."));
        return;
    }

    bool authenticated = client.Login(
        static_cast<const char*>(usernameUtf8),
        static_cast<const char*>(passwordUtf8));

    std::string errorMessage = client.GetLastErrorMessage();

    client.Disconnect();

    if (!authenticated)
    {
        CA2T errorText(errorMessage.c_str(), CP_UTF8);

        SetDlgItemText(
            IDC_LOGIN_ERROR,
            errorMessage.empty()
            ? _T("Invalid username or password.")
            : static_cast<LPCTSTR>(errorText));

        return;
    }

    bool autoRunSuccess = false;

    if (m_checkAutoRun.GetCheck() == BST_CHECKED)
        autoRunSuccess = EnableAutoRun();
    else
        autoRunSuccess = DisableAutoRun();

    if (!autoRunSuccess)
    {
        AfxMessageBox(
            _T("Cannot update Windows startup setting."));
        return;
    }

    EndDialog(IDOK);
}

CString CLoginDlg::GetUsername() const
{
    return m_strUsername;
}

CString CLoginDlg::GetPassword() const
{
    return m_strPassword;
}


void CLoginDlg::OnBnClickedButtonRegister()
{
	// TODO: Add your control notification handler code here

	CRegisterDlg dlg(this);
	dlg.DoModal();
}

