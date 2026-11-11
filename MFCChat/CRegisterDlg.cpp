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

    ON_WM_CTLCOLOR()
    ON_WM_DRAWITEM()
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

// ------------------------------------------------------------------ Giao diện

BOOL CRegisterDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    m_theme.Init(this);

    SetWindowText(_T("MFC Chat - Đăng ký"));

    const int margin = 40;
    const int width = 320;

    m_theme.ResizeClient(this, 400, 410);

    m_theme.CreateHeader(
        this, _T("Tạo tài khoản"), _T("Điền thông tin để bắt đầu trò chuyện"),
        margin, 30, width);

    // Ẩn các nhãn cũ; dùng chữ gợi ý trong ô nhập.
    m_theme.HideOtherChildren(this, {
        IDC_REGISTER_USERNAME, IDC_REGISTER_PASSWORD,
        IDC_REGISTER_CONFIRM_PASSWORD, IDC_REGISTER_ERROR,
        IDC_BUTTON_REGISTER, IDCANCEL });

    m_theme.StyleEdit(
        this, IDC_REGISTER_USERNAME, _T("Tên đăng nhập"),
        m_theme.Box(margin, 112, width, 36));

    m_theme.StyleEdit(
        this, IDC_REGISTER_PASSWORD, _T("Mật khẩu"),
        m_theme.Box(margin, 160, width, 36));

    m_theme.StyleEdit(
        this, IDC_REGISTER_CONFIRM_PASSWORD, _T("Nhập lại mật khẩu"),
        m_theme.Box(margin, 208, width, 36));

    // Nhãn báo lỗi: chữ đỏ, căn giữa, ban đầu để trống.
    SetDlgItemText(IDC_REGISTER_ERROR, _T(""));

    if (CWnd* error = GetDlgItem(IDC_REGISTER_ERROR))
        error->ModifyStyle(SS_TYPEMASK, SS_CENTER);

    m_theme.Place(
        this, IDC_REGISTER_ERROR,
        m_theme.Box(margin, 256, width, 36), &m_theme.fontSmall);

    m_theme.MakeOwnerDraw(
        this, IDC_BUTTON_REGISTER, _T("Đăng ký"),
        m_theme.Box(margin, 300, width, 42));

    m_theme.MakeOwnerDraw(
        this, IDCANCEL, _T("Hủy"),
        m_theme.Box(margin, 352, width, 38));

    return TRUE;
}

// Nhấn Enter trong ô nhập = bấm nút Đăng ký.
void CRegisterDlg::OnOK()
{
    OnBnClickedButtonRegister();
}

HBRUSH CRegisterDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH brush = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

    dlgtheme::ApplyCtlColor(pDC, pWnd, IDC_REGISTER_ERROR);

    return brush;
}

void CRegisterDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
    if (lpDrawItemStruct != nullptr &&
        (nIDCtl == IDC_BUTTON_REGISTER || nIDCtl == IDCANCEL))
    {
        // Đăng ký = nút chính (xanh); Hủy = nút phụ.
        dlgtheme::DrawButton(
            lpDrawItemStruct, &m_theme.fontButton, nIDCtl == IDC_BUTTON_REGISTER);
        return;
    }

    CDialogEx::OnDrawItem(nIDCtl, lpDrawItemStruct);
}