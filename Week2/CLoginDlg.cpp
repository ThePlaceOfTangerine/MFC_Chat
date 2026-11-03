// CLoginDlg.cpp : implementation file
//

#include "pch.h"
#include "Week2.h"
#include "CLoginDlg.h"
#include "CRegisterDlg.h"
#include "afxdialogex.h"
#include <map>

static std::map<CString, CString> g_users = { { _T("admin"), _T("123") } };

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
	DDX_Text(pDX, IDC_EDIT_USERNAME, m_strUsername);
	DDX_Text(pDX, IDC_EDIT_PASSWORD, m_strPassword);
}


BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
	ON_BN_CLICKED(IDC_BTN_LOGIN, &CLoginDlg::OnBnClickedBtnLogin)
	ON_BN_CLICKED(IDC_BTN_REGISTER, &CLoginDlg::OnBnClickedBtnRegister)
END_MESSAGE_MAP()


// CLoginDlg message handlers


void CLoginDlg::OnBnClickedBtnLogin()
{
	TCHAR user[64] = { 0 }, pass[64] = { 0 };
	::GetDlgItemText(m_hWnd, IDC_EDIT_USERNAME, user, 64);
	::GetDlgItemText(m_hWnd, IDC_EDIT_PASSWORD, pass, 64);

	auto it = g_users.find(user);
	if (it == g_users.end() || it->second != pass)
	{
		::MessageBox(m_hWnd, _T("Sai tai khoan hoac mat khau"), _T("Loi"), MB_OK | MB_ICONWARNING);
		return;
	}

	m_strUsername = user;
	EndDialog(IDOK);
}

void CLoginDlg::OnBnClickedBtnRegister()
{
	CRegisterDlg dlg;
	if (dlg.DoModal() != IDOK)
		return;                       // bấm Hủy

	if (g_users.count(dlg.m_strUser))
	{
		::MessageBox(m_hWnd, _T("Tai khoan da ton tai"), _T("Loi"), MB_OK);
		return;
	}

	g_users[dlg.m_strUser] = dlg.m_strPass;

	// Điền sẵn tên vừa đăng ký vào ô đăng nhập cho tiện
	::SetDlgItemText(m_hWnd, IDC_EDIT_USERNAME, dlg.m_strUser);
	::SetDlgItemText(m_hWnd, IDC_EDIT_PASSWORD, _T(""));
	::MessageBox(m_hWnd, _T("Dang ky thanh cong, hay dang nhap"), _T("OK"), MB_OK);
}
