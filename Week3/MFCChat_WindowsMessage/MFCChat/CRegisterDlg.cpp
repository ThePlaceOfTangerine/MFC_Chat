// CRegisterDlg.cpp : implementation file
//

#include "pch.h"
#include "MFCChat.h"
#include "CRegisterDlg.h"
#include "afxdialogex.h"
#include "Database.h"
#include <atlconv.h>


// CRegisterDlg dialog

IMPLEMENT_DYNAMIC(CRegisterDlg, CDialogEx)

CRegisterDlg::CRegisterDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_REGISTER_DIALOG, pParent)
	, m_strUsername(_T(""))
	, m_strPassword(_T(""))
	, m_strConfirmPassword(_T(""))
{

}

CRegisterDlg::~CRegisterDlg()
{
}

void CRegisterDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_REGISTER_USERNAME, m_strUsername);
	DDX_Text(pDX, IDC_REGISTER_PASSWORD, m_strPassword);
	DDX_Text(pDX, IDC_REGISTER_CONFIRM_PASSWORD, m_strConfirmPassword);
}


BEGIN_MESSAGE_MAP(CRegisterDlg, CDialogEx)
	ON_BN_CLICKED(IDC_BUTTON_REGISTER, &CRegisterDlg::OnBnClickedButtonRegister)
	ON_BN_CLICKED(IDCANCEL, &CRegisterDlg::OnBnClickedCancel)
END_MESSAGE_MAP()


// CRegisterDlg message handlers


void CRegisterDlg::OnBnClickedButtonRegister()
{
    if (!UpdateData(TRUE))
        return;

    if (m_strUsername.IsEmpty() || m_strPassword.IsEmpty() || m_strConfirmPassword.IsEmpty())
    {
        SetDlgItemText(IDC_REGISTER_ERROR, _T("Please fill in all fields."));
        return;
    }

    if (m_strConfirmPassword != m_strPassword)
    {
        SetDlgItemText(IDC_REGISTER_ERROR, _T("Passwords do not match."));
        return;
    }

    if (m_strUsername.Find(_T(" ")) != -1)
    {
        SetDlgItemText(IDC_REGISTER_ERROR, _T("Username cannot contain spaces."));
        return;
    }

    Database db;

    if (!db.Open("D:\\Work\\CMC\\MFCChat\\chat.db"))
    {
        AfxMessageBox(_T("Cannot open database."), MB_ICONERROR);
        return;
    }

    if (!db.Initialize())
    {
        db.Close();

        AfxMessageBox(_T("Cannot initialize database."), MB_ICONERROR);
        return;
    }

    CT2A username(m_strUsername, CP_UTF8);
    CT2A password(m_strPassword, CP_UTF8);

    bool success = db.CreateUser(username, password, username);

    db.Close();

    if (!success)
    {
        SetDlgItemText(IDC_REGISTER_ERROR, _T("Registration failed. Username may already exist."));
        return;
    }

    AfxMessageBox(_T("Register successful!"));

    EndDialog(IDOK);
}


void CRegisterDlg::OnBnClickedCancel()
{
	// TODO: Add your control notification handler code here
	CDialogEx::OnCancel();
}
