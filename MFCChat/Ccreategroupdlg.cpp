// CCreateGroupDlg.cpp : hộp thoại tạo nhóm chat
//

#include "pch.h"
#include "framework.h"
#include "CCreateGroupDlg.h"

namespace
{
	// ID các control tạo bằng code (không trùng với resource.h).
	const UINT kEditNameId = 3002;
	const UINT kLabelMembersId = 3003;
	const UINT kListId = 3004;
}

BEGIN_MESSAGE_MAP(CCreateGroupDlg, CDialogEx)
	ON_WM_CTLCOLOR()
	ON_WM_DRAWITEM()
	ON_LBN_SELCHANGE(kListId, &CCreateGroupDlg::OnSelChange)
END_MESSAGE_MAP()

CCreateGroupDlg::CCreateGroupDlg(
	const std::vector<CString>& candidates,
	CWnd* pParent)
	: m_candidates(candidates)
{
	BuildTemplate();

	InitModalIndirect(
		reinterpret_cast<LPCDLGTEMPLATE>(m_template.data()),
		pParent);
}

BOOL CCreateGroupDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	m_theme.Init(this);

	SetWindowText(_T("Tạo nhóm mới"));

	const int clientWidth = 380;
	const int clientHeight = 470;
	const int margin = 32;
	const int width = clientWidth - 2 * margin;

	m_theme.ResizeClient(this, clientWidth, clientHeight);

	m_theme.CreateHeader(
		this, _T("Tạo nhóm mới"), _T("Đặt tên và chọn thành viên cho nhóm"),
		margin, 24, width);

	m_editName.Create(
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
		m_theme.Box(margin, 100, width, 36), this, kEditNameId);
	m_editName.SetLimitText(50);
	m_editName.SetFont(&m_theme.fontInput);
	m_editName.SetCueBanner(_T("Tên nhóm"), TRUE);

	m_labelMembers.Create(
		_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
		m_theme.Box(margin, 152, width, 22), this, kLabelMembersId);
	m_labelMembers.SetFont(&m_theme.fontSmall);

	m_list.Create(
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
		LBS_MULTIPLESEL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		m_theme.Box(margin, 178, width, clientHeight - 178 - 88),
		this, kListId);
	m_list.SetFont(&m_theme.fontInput);
	m_list.SetItemHeight(0, m_theme.S(30));

	for (const CString& name : m_candidates)
		m_list.AddString(name);

	// Hai nút nằm cạnh nhau ở đáy: Hủy (phụ) và Tạo nhóm (chính).
	const int buttonTop = clientHeight - 64;
	const int cancelWidth = 110;
	const int gap = 12;

	m_cancel.Create(
		_T("Hủy"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
		m_theme.Box(margin, buttonTop, cancelWidth, 40),
		this, IDCANCEL);

	m_ok.Create(
		_T("Tạo nhóm"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
		m_theme.Box(
			margin + cancelWidth + gap, buttonTop,
			width - cancelWidth - gap, 40),
		this, IDOK);

	UpdateSelectedCount();

	m_editName.SetFocus();

	return FALSE;   // đã tự đặt focus
}

void CCreateGroupDlg::OnOK()
{
	m_editName.GetWindowText(m_groupName);
	m_groupName.Trim();

	if (m_groupName.IsEmpty())
	{
		AfxMessageBox(_T("Vui lòng nhập tên nhóm."));
		m_editName.SetFocus();
		return;
	}

	m_selected.clear();

	for (int i = 0; i < m_list.GetCount(); ++i)
	{
		if (m_list.GetSel(i) > 0)
		{
			CString name;
			m_list.GetText(i, name);
			m_selected.push_back(name);
		}
	}

	if (m_selected.empty())
	{
		AfxMessageBox(_T("Hãy chọn ít nhất một thành viên."));
		return;
	}

	CDialogEx::OnOK();
}

HBRUSH CCreateGroupDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH brush = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

	dlgtheme::ApplyCtlColor(pDC, pWnd, 0, kLabelMembersId);

	return brush;
}

void CCreateGroupDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (lpDrawItemStruct != nullptr &&
		(nIDCtl == IDOK || nIDCtl == IDCANCEL))
	{
		// Tạo nhóm = nút chính (xanh); Hủy = nút phụ.
		dlgtheme::DrawButton(
			lpDrawItemStruct, &m_theme.fontButton, nIDCtl == IDOK);
		return;
	}

	CDialogEx::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CCreateGroupDlg::OnSelChange()
{
	UpdateSelectedCount();
}

void CCreateGroupDlg::UpdateSelectedCount()
{
	CString text;
	text.Format(_T("Thành viên (đã chọn %d)"), m_list.GetSelCount());

	m_labelMembers.SetWindowText(text);
}

void CCreateGroupDlg::BuildTemplate()
{
	const wchar_t title[] = L"Tạo nhóm mới";
	const wchar_t fontName[] = L"Segoe UI";

	DLGTEMPLATE dt = {};
	dt.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT |
		WS_POPUP | WS_CAPTION | WS_SYSMENU;
	dt.cdit = 0;
	dt.x = 0;
	dt.y = 0;
	dt.cx = 190;   // dialog units (kích thước thật được đặt lại trong OnInitDialog)
	dt.cy = 200;

	auto append = [this](const void* data, size_t size)
	{
		const BYTE* bytes = static_cast<const BYTE*>(data);
		m_template.insert(m_template.end(), bytes, bytes + size);
	};

	append(&dt, sizeof(dt));

	const WORD menu = 0;     // không có menu
	const WORD cls = 0;      // lớp dialog mặc định
	append(&menu, sizeof(menu));
	append(&cls, sizeof(cls));
	append(title, sizeof(title));        // gồm cả ký tự null cuối

	const WORD pointSize = 9;
	append(&pointSize, sizeof(pointSize));
	append(fontName, sizeof(fontName));
}