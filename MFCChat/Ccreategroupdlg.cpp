// CCreateGroupDlg.cpp : hộp thoại tạo nhóm chat
//

#include "pch.h"
#include "framework.h"
#include "CCreateGroupDlg.h"

namespace
{
	// ID các control tạo bằng code (không trùng với resource.h).
	const UINT kLabelNameId = 3001;
	const UINT kEditNameId = 3002;
	const UINT kLabelMembersId = 3003;
	const UINT kListId = 3004;
}

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

	SetWindowText(_T("Tạo nhóm mới"));

	CRect rc;
	GetClientRect(&rc);

	const int pad = 10;
	const int width = rc.Width() - 2 * pad;

	m_labelName.Create(
		_T("Tên nhóm:"), WS_CHILD | WS_VISIBLE,
		CRect(pad, 10, pad + width, 28), this, kLabelNameId);

	m_editName.Create(
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
		CRect(pad, 30, pad + width, 54), this, kEditNameId);
	m_editName.SetLimitText(50);

	m_labelMembers.Create(
		_T("Chọn thành viên (bấm để chọn / bỏ chọn):"),
		WS_CHILD | WS_VISIBLE,
		CRect(pad, 64, pad + width, 82), this, kLabelMembersId);

	const int buttonTop = rc.Height() - 38;

	m_list.Create(
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
		WS_VSCROLL | LBS_MULTIPLESEL | LBS_NOTIFY,
		CRect(pad, 84, pad + width, buttonTop - 10), this, kListId);

	for (const CString& name : m_candidates)
		m_list.AddString(name);

	const int buttonWidth = 80;

	m_ok.Create(
		_T("Tạo"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		CRect(rc.Width() - pad - 2 * buttonWidth - 8, buttonTop,
			rc.Width() - pad - buttonWidth - 8, buttonTop + 26),
		this, IDOK);

	m_cancel.Create(
		_T("Hủy"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		CRect(rc.Width() - pad - buttonWidth, buttonTop,
			rc.Width() - pad, buttonTop + 26),
		this, IDCANCEL);

	CFont* font = GetFont();

	m_labelName.SetFont(font);
	m_editName.SetFont(font);
	m_labelMembers.SetFont(font);
	m_list.SetFont(font);
	m_ok.SetFont(font);
	m_cancel.SetFont(font);

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
	dt.cx = 190;   // dialog units
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