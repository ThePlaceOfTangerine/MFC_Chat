#pragma once

#include <vector>
#include "afxdialogex.h"

// Hộp thoại "Tạo nhóm": nhập tên nhóm và chọn các thành viên.
// Giao diện được dựng hoàn toàn bằng code (không cần resource trong file .rc).
//
// Cách dùng:
//     CCreateGroupDlg dlg(danhSachUsername, this);
//     if (dlg.DoModal() == IDOK)
//     {
//         CString ten = dlg.GetGroupName();
//         const std::vector<CString>& thanhVien = dlg.GetSelectedMembers();
//     }
class CCreateGroupDlg : public CDialogEx
{
public:
	CCreateGroupDlg(
		const std::vector<CString>& candidates,
		CWnd* pParent = nullptr);

	// Kết quả hợp lệ sau khi DoModal() trả về IDOK.
	const CString& GetGroupName() const { return m_groupName; }
	const std::vector<CString>& GetSelectedMembers() const { return m_selected; }

protected:
	virtual BOOL OnInitDialog() override;
	virtual void OnOK() override;

private:
	// Dựng DLGTEMPLATE trong bộ nhớ: chỉ có khung và tiêu đề,
	// các control được tạo trong OnInitDialog.
	void BuildTemplate();

	std::vector<CString> m_candidates;   // username có thể chọn
	std::vector<BYTE> m_template;        // dữ liệu DLGTEMPLATE

	CString m_groupName;
	std::vector<CString> m_selected;

	CStatic m_labelName;
	CEdit m_editName;
	CStatic m_labelMembers;
	CListBox m_list;
	CButton m_ok;
	CButton m_cancel;
};