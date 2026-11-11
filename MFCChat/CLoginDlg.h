#pragma once

#include "afxdialogex.h"
#include "DialogTheme.h"

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

protected:
    virtual void OnOK() override;   // Enter = đăng nhập

    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);

private:
    void ApplyStyle();

    dlgtheme::Theme m_theme;
};