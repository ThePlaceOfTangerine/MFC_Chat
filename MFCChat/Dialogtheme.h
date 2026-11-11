#pragma once

#include <initializer_list>
#include "afxdialogex.h"

// Giao diện chung cho các hộp thoại nhỏ (đăng nhập, đăng ký, tạo nhóm):
// nền trắng, tiêu đề lớn, ô nhập có chữ gợi ý, nút xanh phẳng bo góc.
// File chỉ gồm header nên không cần thêm vào project.
namespace dlgtheme
{
	const COLORREF kBackground = RGB(255, 255, 255);
	const COLORREF kAccent = RGB(0, 120, 215);
	const COLORREF kAccentPressed = RGB(0, 90, 170);
	const COLORREF kText = RGB(32, 32, 32);
	const COLORREF kMuted = RGB(110, 118, 130);
	const COLORREF kError = RGB(200, 35, 45);
	const COLORREF kBorder = RGB(196, 202, 212);
	const COLORREF kSecondaryFill = RGB(240, 243, 248);
	const COLORREF kSecondaryPressed = RGB(222, 228, 237);

	// ID của hai nhãn tiêu đề tạo bằng code (không trùng resource.h).
	const UINT kTitleId = 3101;
	const UINT kSubtitleId = 3102;

	class Theme
	{
	public:
		CFont fontTitle;
		CFont fontSubtitle;
		CFont fontInput;
		CFont fontButton;
		CFont fontSmall;

		CStatic titleLabel;
		CStatic subtitleLabel;

		// Tạo font, đặt nền trắng cho hộp thoại.
		void Init(CDialogEx* dialog)
		{
			CClientDC dc(dialog);

			m_dpi = dc.GetDeviceCaps(LOGPIXELSY);

			if (m_dpi <= 0)
				m_dpi = 96;

			MakeFont(fontTitle, 20, true);
			MakeFont(fontSubtitle, 10, false);
			MakeFont(fontInput, 11, false);
			MakeFont(fontButton, 10, true);
			MakeFont(fontSmall, 10, false);

			dialog->SetBackgroundColor(kBackground);
		}

		// Đổi kích thước thiết kế (pixel ở 96 DPI) sang pixel thật.
		int S(int px) const { return MulDiv(px, m_dpi, 96); }

		CRect Box(int x, int y, int width, int height) const
		{
			return CRect(S(x), S(y), S(x + width), S(y + height));
		}

		// Đặt kích thước vùng client của hộp thoại rồi căn giữa màn hình.
		void ResizeClient(CWnd* dialog, int width, int height) const
		{
			CRect rect(0, 0, S(width), S(height));

			::AdjustWindowRectEx(
				&rect, dialog->GetStyle(), FALSE, dialog->GetExStyle());

			dialog->SetWindowPos(
				nullptr, 0, 0, rect.Width(), rect.Height(),
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

			dialog->CenterWindow();
		}

		// Tiêu đề lớn và dòng phụ màu xám ở đầu hộp thoại.
		void CreateHeader(
			CWnd* dialog,
			LPCTSTR titleText,
			LPCTSTR subtitleText,
			int x, int y, int width)
		{
			titleLabel.Create(
				titleText, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				Box(x, y, width, 34), dialog, kTitleId);
			titleLabel.SetFont(&fontTitle);

			subtitleLabel.Create(
				subtitleText, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
				Box(x, y + 38, width, 22), dialog, kSubtitleId);
			subtitleLabel.SetFont(&fontSubtitle);
		}

		// Ẩn các control cũ không dùng nữa (nhãn "Username:", khung nhóm...).
		void HideOtherChildren(
			CWnd* dialog, std::initializer_list<UINT> keep) const
		{
			for (CWnd* child = dialog->GetWindow(GW_CHILD);
				child != nullptr;
				child = child->GetNextWindow())
			{
				const UINT id = static_cast<UINT>(child->GetDlgCtrlID());

				bool keepIt = (id == kTitleId || id == kSubtitleId);

				for (UINT wanted : keep)
				{
					if (wanted == id)
						keepIt = true;
				}

				if (!keepIt)
					child->ShowWindow(SW_HIDE);
			}
		}

		void Place(
			CWnd* dialog, UINT id, const CRect& rect, CFont* font) const
		{
			CWnd* control = dialog->GetDlgItem(id);

			if (control == nullptr)
				return;

			control->SetWindowPos(
				nullptr, rect.left, rect.top, rect.Width(), rect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE);

			if (font != nullptr)
				control->SetFont(font);
		}

		// Ô nhập: viền mảnh, chữ lớn, chữ gợi ý khi để trống.
		void StyleEdit(
			CWnd* dialog, UINT id, LPCTSTR cueText, const CRect& rect)
		{
			CEdit* edit = static_cast<CEdit*>(dialog->GetDlgItem(id));

			if (edit == nullptr)
				return;

			edit->ModifyStyleEx(WS_EX_CLIENTEDGE, 0, SWP_FRAMECHANGED);
			edit->ModifyStyle(0, WS_BORDER, SWP_FRAMECHANGED);

			Place(dialog, id, rect, &fontInput);

			edit->SetCueBanner(cueText, TRUE);
		}

		// Chuyển nút có sẵn sang kiểu tự vẽ (xem DrawButton) và đặt chữ.
		void MakeOwnerDraw(
			CWnd* dialog, UINT id, LPCTSTR text, const CRect& rect) const
		{
			CWnd* button = dialog->GetDlgItem(id);

			if (button == nullptr)
				return;

			button->ModifyStyle(BS_TYPEMASK, BS_OWNERDRAW);
			button->SetWindowText(text);

			Place(dialog, id, rect, nullptr);
			button->Invalidate();
		}

	private:
		void MakeFont(CFont& font, int pointSize, bool bold)
		{
			if (font.GetSafeHandle() != nullptr)
				return;

			font.CreateFont(
				-MulDiv(pointSize, m_dpi, 72), 0, 0, 0,
				bold ? FW_BOLD : FW_NORMAL,
				FALSE, FALSE, FALSE,
				DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS,
				CLIP_DEFAULT_PRECIS,
				CLEARTYPE_QUALITY,
				DEFAULT_PITCH | FF_DONTCARE,
				_T("Segoe UI"));
		}

		int m_dpi = 96;
	};

	// Vẽ nút phẳng bo góc: primary = nền xanh chữ trắng,
	// còn lại = nền xám nhạt có viền.
	inline void DrawButton(LPDRAWITEMSTRUCT dis, CFont* font, bool primary)
	{
		if (dis == nullptr)
			return;

		CDC* dc = CDC::FromHandle(dis->hDC);
		CRect rect(dis->rcItem);

		const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
		const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
		const bool focused = (dis->itemState & ODS_FOCUS) != 0;

		COLORREF fill;
		COLORREF border;
		COLORREF text;

		if (primary)
		{
			fill = disabled ? RGB(170, 200, 230)
				: (pressed ? kAccentPressed : kAccent);
			border = fill;
			text = RGB(255, 255, 255);
		}
		else
		{
			fill = pressed ? kSecondaryPressed : kSecondaryFill;
			border = focused ? kAccent : kBorder;
			text = disabled ? kMuted : kText;
		}

		dc->FillSolidRect(rect, kBackground);

		CBrush brush(fill);
		CPen pen(PS_SOLID, 1, border);

		CBrush* oldBrush = dc->SelectObject(&brush);
		CPen* oldPen = dc->SelectObject(&pen);

		CRect shape(rect);
		shape.DeflateRect(0, 0, 1, 1);   // RoundRect không tính cạnh phải/dưới

		dc->RoundRect(shape, CPoint(10, 10));

		dc->SelectObject(oldBrush);
		dc->SelectObject(oldPen);

		CString caption;
		CWnd::FromHandle(dis->hwndItem)->GetWindowText(caption);

		CFont* oldFont = (font != nullptr) ? dc->SelectObject(font) : nullptr;

		dc->SetBkMode(TRANSPARENT);
		dc->SetTextColor(text);
		dc->DrawText(caption, rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		if (oldFont != nullptr)
			dc->SelectObject(oldFont);
	}

	// Gọi trong OnCtlColor: tô màu tiêu đề, dòng phụ, nhãn lỗi và nhãn mờ.
	inline void ApplyCtlColor(
		CDC* dc, CWnd* control, UINT errorId = 0, UINT mutedId = 0)
	{
		if (dc == nullptr || control == nullptr)
			return;

		const UINT id = static_cast<UINT>(control->GetDlgCtrlID());

		if (id == kTitleId)
		{
			dc->SetTextColor(kText);
			dc->SetBkMode(TRANSPARENT);
		}
		else if (id == kSubtitleId || (mutedId != 0 && id == mutedId))
		{
			dc->SetTextColor(kMuted);
			dc->SetBkMode(TRANSPARENT);
		}
		else if (errorId != 0 && id == errorId)
		{
			dc->SetTextColor(kError);
			dc->SetBkMode(TRANSPARENT);
		}
	}
}