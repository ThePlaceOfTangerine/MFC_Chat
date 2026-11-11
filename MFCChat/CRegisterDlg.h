#pragma once

#include "DialogTheme.h"


// CRegisterDlg dialog

class CRegisterDlg : public CDialogEx
{
	DECLARE_DYNAMIC(CRegisterDlg)

public:
	CRegisterDlg(CWnd* pParent = nullptr);   // standard constructor
	virtual ~CRegisterDlg();

	// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_REGISTER_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	CString m_strUsername;
	CString m_strPassword;
	CString m_strConfirmPassword;

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedButtonRegister();
	afx_msg void OnBnClickedCancel();

protected:
	virtual BOOL OnInitDialog() override;
	virtual void OnOK() override;   // Enter = đăng ký

	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

private:
	dlgtheme::Theme m_theme;
};