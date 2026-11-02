#include "pch.h"
#include "MFCChat.h"
#include "CRegisterDlg.h"
#include "afxdialogex.h"
#include "TcpChatClient.h"

#include <atlconv.h>
#include <string>

IMPLEMENT_DYNAMIC(CRegisterDlg, CDialogEx)

CRegisterDlg::CRegisterDlg(CWnd* pParent)
    : CDialogEx(IDD_REGISTER_DIALOG, pParent),
    m_strUsername(_T("")),
    m_strPassword(_T("")),
    m_strConfirmPassword(_T(""))
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
    DDX_Text(
        pDX,
        IDC_REGISTER_CONFIRM_PASSWORD,
        m_strConfirmPassword
    );
}

BEGIN_MESSAGE_MAP(CRegisterDlg, CDialogEx)
    ON_BN_CLICKED(
        IDC_BUTTON_REGISTER,
        &CRegisterDlg::OnBnClickedButtonRegister
    )

    ON_BN_CLICKED(
        IDCANCEL,
        &CRegisterDlg::OnBnClickedCancel
    )
END_MESSAGE_MAP()

void CRegisterDlg::OnBnClickedButtonRegister()
{
    if (!UpdateData(TRUE))
        return;

    if (m_strUsername.IsEmpty() ||
        m_strPassword.IsEmpty() ||
        m_strConfirmPassword.IsEmpty())
    {
        SetDlgItemText(
            IDC_REGISTER_ERROR,
            _T("Please fill in all fields.")
        );

        return;
    }

    if (m_strConfirmPassword != m_strPassword)
    {
        SetDlgItemText(
            IDC_REGISTER_ERROR,
            _T("Passwords do not match.")
        );

        return;
    }

    if (m_strUsername.Find(_T(" ")) != -1)
    {
        SetDlgItemText(
            IDC_REGISTER_ERROR,
            _T("Username cannot contain spaces.")
        );

        return;
    }

    CT2A usernameUtf8(m_strUsername, CP_UTF8);
    CT2A passwordUtf8(m_strPassword, CP_UTF8);

    std::string username(
        static_cast<const char*>(usernameUtf8)
    );

    std::string password(
        static_cast<const char*>(passwordUtf8)
    );

    // Dùng username làm display name mặc định.
    std::string displayName = username;

    TcpChatClient client;

    if (!client.Connect("127.0.0.1", 5000))
    {
        SetDlgItemText(
            IDC_REGISTER_ERROR,
            _T("Cannot connect to TCP server.")
        );

        return;
    }

    bool success = client.Register(
        username,
        password,
        displayName
    );

    std::string error = client.GetLastErrorMessage();

    client.Disconnect();

    if (!success)
    {
        CA2T errorText(error.c_str(), CP_UTF8);

        CString message;

        message.Format(
            _T("Registration failed: %s"),
            error.empty()
            ? _T("Unknown error")
            : static_cast<LPCTSTR>(errorText)
        );

        SetDlgItemText(IDC_REGISTER_ERROR, message);
        return;
    }

    AfxMessageBox(
        _T("Registration successful!"),
        MB_ICONINFORMATION
    );

    EndDialog(IDOK);
}

void CRegisterDlg::OnBnClickedCancel()
{
    CDialogEx::OnCancel();
}