// CRegisterDlg.cpp : implementation file
//

#include "pch.h"
#include "Week2.h"
#include "CRegisterDlg.h"
#include "afxdialogex.h"


// CRegisterDlg dialog

IMPLEMENT_DYNAMIC(CRegisterDlg, CDialogEx)

CRegisterDlg::CRegisterDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_REGISTER_DIALOG, pParent)
{

}

CRegisterDlg::~CRegisterDlg()
{
}

void CRegisterDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CRegisterDlg, CDialogEx)
END_MESSAGE_MAP()


// CRegisterDlg message handlers
void CRegisterDlg::OnOK()
{
	TCHAR user[64] = { 0 }, pass[64] = { 0 }, confirm[64] = { 0 };
	::GetDlgItemText(m_hWnd, IDC_REG_USER, user, 64);
	::GetDlgItemText(m_hWnd, IDC_REG_PASS, pass, 64);
	::GetDlgItemText(m_hWnd, IDC_REG_CONFIRM, confirm, 64);

	if (user[0] == 0 || pass[0] == 0)
	{
		::MessageBox(m_hWnd, _T("Nhap day du thong tin"), _T("Loi"), MB_OK | MB_ICONWARNING);
		return;                       // không đóng dialog
	}
	if (_tcscmp(pass, confirm) != 0)
	{
		::MessageBox(m_hWnd, _T("Mat khau xac nhan khong khop"), _T("Loi"), MB_OK | MB_ICONWARNING);
		return;
	}

	m_strUser = user;
	m_strPass = pass;
	CDialogEx::OnOK();                // đóng dialog, trả IDOK
}
