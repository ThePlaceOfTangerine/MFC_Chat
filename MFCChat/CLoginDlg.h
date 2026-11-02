#pragma once

#include "afxdialogex.h"

class CLoginDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CLoginDlg)

public:
    CLoginDlg(CWnd* pParent = nullptr);
    virtual ~CLoginDlg();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_LOGIN_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    CString m_strUsername;
    CString m_strPassword;

    DECLARE_MESSAGE_MAP()

public:
    afx_msg void OnBnClickedButtonRegister();
    afx_msg void OnBnClickedButtonLogin();

    CString GetUsername() const;
    CString GetPassword() const;

    CButton m_checkAutoRun;
};