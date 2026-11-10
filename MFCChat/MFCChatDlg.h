// MFCChatDlg.h : header file
//

#pragma once

#include "ChatProtocol.h"
#include "TcpChatClient.h"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <memory>
#include <atomic>
#include <thread>

// ID của nút "Tạo nhóm" (nút được tạo bằng code, không nằm trong file .rc)
#define IDC_BUTTON_CREATE_GROUP 2001
#define IDC_STATIC_SUBTITLE 2002
#define IDC_BUTTON_ATTACH 2003
#define IDC_BUTTON_PEER_AVATAR 2004
#define IDC_BUTTON_MY_AVATAR 2005

// Thread gửi tệp báo tiến độ về cửa sổ: wParam = phần trăm, lParam = 1 nếu lỗi.
#define WM_APP_UPLOAD_PROGRESS (WM_APP + 120)

// Một tệp đính kèm hiển thị dạng liên kết trong khung chat.
struct ChatAttachmentLink
{
	long start = 0;          // vị trí trong RichEdit [start, end)
	long end = 0;
	int64_t id = 0;
	std::string kind;        // "image" | "video"
	std::string name;
	int64_t size = 0;

	bool thumb = false;      // server có ảnh thu nhỏ cho tệp này
	long imagePos = -1;      // vị trí ký tự của ảnh thu nhỏ trong RichEdit
	int imageWidth = 0;
	int imageHeight = 0;
};

// Ảnh thu nhỏ đã giải mã (giữ HBITMAP để chèn vào RichEdit nhiều lần).
struct ChatThumbnail
{
	HBITMAP bitmap = nullptr;
	int width = 0;
	int height = 0;
	std::string jpeg;        // dữ liệu JPEG gốc (dùng cho đường dự phòng RTF)
};

struct IRichEditOleCallback;


// CMFCChatDlg dialog
class CMFCChatDlg : public CDialogEx
{
	// Construction
public:
	CMFCChatDlg(CWnd* pParent = nullptr);

	void SetCurrentUser(const CString& username);
	void SetCurrentPassword(const CString& password);

	CString GetCurrentUser() const;

	afx_msg void OnBnClickedButtonSend();
	afx_msg void OnLvnItemchangedListConversation(NMHDR* pNMHDR, LRESULT* pResult);

	BOOL SendChatMessage(const CString& message);

	UINT m_chatMessageId = 0;

	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

	void RefreshConversationList();
	UINT m_refreshMessageId = 0;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	// Implementation
	HICON m_hIcon;

	virtual BOOL OnInitDialog();
	virtual void OnOK();   // Enter trong ô nhập = gửi tin

	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg LRESULT OnTcpMessage(WPARAM wParam, LPARAM lParam);

	// Receive Windows Message
	afx_msg BOOL OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct);

	afx_msg LRESULT OnTcpResponse(WPARAM wParam, LPARAM lParam);

	void DisplayConversationResponse(const std::string& payload);

	void DisplayHistoryResponse(const std::string& payload);

	// ---- Group chat ----
	afx_msg void OnBnClickedCreateGroup();

	// ---- Giao diện ----
	afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnCustomDrawConversationList(NMHDR* pNMHDR, LRESULT* pResult);

	// ---- Avatar, gửi ảnh/video ----
	afx_msg void OnBnClickedAttach();
	afx_msg void OnBnClickedMyAvatar();
	afx_msg void OnDropFiles(HDROP hDropInfo);
	afx_msg void OnDestroy();
	afx_msg void OnRichEditLink(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnRichEditMsgFilter(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnUploadProgress(WPARAM wParam, LPARAM lParam);
	void DisplayGroupsResponse(const std::string& payload);
	void DisplayGroupHistoryResponse(const std::string& payload);

	CListCtrl m_conversationList;
	CMapStringToString m_fakeChats;

	DECLARE_MESSAGE_MAP()

private:
	CString m_currentUser;
	CString m_currentPassword;
	CString m_currentChatUser;
	CString m_messageHistory;
	void LoadChatHistory();
	TcpChatClient m_tcpClient;

	// Một dòng trong danh sách bên trái (người dùng hoặc nhóm).
	struct ConversationItem
	{
		CString name;
		CString preview;
		CString time;
		int64_t groupId = 0;   // 0 = hội thoại 1-1, > 0 = nhóm
		int avatarVersion = 0; // phiên bản ảnh đại diện (0 = chưa có)
	};

	std::vector<ConversationItem> m_userItems;
	std::vector<ConversationItem> m_groupItems;

	void RebuildConversationList();
	void ApplySelection(int index);
	void ApplyChatTheme();

	// ---- Bố cục co giãn theo cửa sổ ----
	struct AnchorInfo
	{
		UINT id = 0;
		CRect initial;        // vị trí ban đầu (toạ độ client)
		DWORD anchors = 0;    // bám cạnh nào khi cửa sổ đổi kích thước
	};

	std::vector<AnchorInfo> m_anchors;
	CSize m_initialClientSize;
	CSize m_minWindowSize;
	bool m_layoutReady = false;

	void RegisterAnchors();
	void LayoutControls(int cx, int cy);
	void FitConversationColumn();

	// ---- Avatar ----
	ULONG_PTR m_gdiplusToken = 0;
	std::map<std::string, std::shared_ptr<void>> m_avatarBitmaps;   // Gdiplus::Bitmap
	std::map<std::string, int> m_avatarVersions;
	std::set<std::string> m_avatarRequested;
	std::string m_pendingAvatarPng;
	CButton m_btnAttach;
	CButton m_btnPeerAvatar;
	CButton m_btnMyAvatar;

	void ApplyMediaUi();
	void SetStatusText(const CString& text);
	void UpdateSubtitle();
	void DrawAvatar(
		HDC hdc,
		const CRect& rect,
		const CString& label,
		const std::string& key,
		bool isGroup);
	void RequestAvatarIfNeeded(const std::string& username, int version);
	void OnAvatarResult(const std::string& payload);
	void OnSetAvatarResult(const std::string& payload);

	// ---- Gửi tệp (upload) ----
	bool m_uploadActive = false;
	std::string m_uploadId;
	CString m_uploadPath;
	CString m_uploadName;
	int64_t m_uploadSize = 0;
	bool m_uploadIsGroup = false;
	int64_t m_uploadGroupId = 0;
	std::string m_uploadReceiver;
	std::atomic<bool> m_uploadCancel{ false };
	std::thread m_uploadThread;

	void StartUpload(const CString& path);
	void FinishUpload();
	void OnUploadReady(const std::string& payload);
	void OnUploadResult(const std::string& payload);
	void UploadWorker(
		HWND hwnd, CString path, std::string uploadId, int64_t size);

	// ---- Tải/mở tệp (download) ----
	std::vector<ChatAttachmentLink> m_attachmentLinks;
	bool m_downloadActive = false;
	int64_t m_downloadId = 0;
	int64_t m_downloadSize = 0;
	int64_t m_downloadOffset = 0;
	FILE* m_downloadFile = nullptr;
	CString m_downloadPartPath;
	CString m_downloadFinalPath;
	CString m_downloadName;

	void OpenAttachment(const ChatAttachmentLink& link);
	void OnAttachmentChunk(const std::string& payload);
	void AbortDownload();

	// ---- Ảnh thu nhỏ hiển thị ngay trong khung chat ----
	std::map<int64_t, ChatThumbnail> m_thumbs;
	std::set<int64_t> m_thumbRequested;   // đã xin (tránh xin lặp, kể cả khi lỗi)
	int m_thumbPending = 0;               // số yêu cầu đang chờ kết quả
	bool m_thumbDirty = false;            // có ảnh mới về, cần vẽ lại lịch sử
	std::string m_lastHistoryPayload;     // để vẽ lại mà không hỏi lại server
	bool m_lastHistoryWasGroup = false;
	std::string m_uploadThumbJpeg;        // ảnh thu nhỏ của tệp đang gửi
	IRichEditOleCallback* m_oleCallback = nullptr;

	void RequestThumbnails(const std::set<int64_t>& ids);
	void OnThumbnailResult(const std::string& payload);
	void ReRenderLastHistory();

	CFont m_fontListName;
	CFont m_fontListPreview;
	CFont m_fontTitle;
	CFont m_fontSubtitle;
	CFont m_fontInput;
	CImageList m_rowHeightImages;   // chỉ để đặt chiều cao dòng của danh sách
	CStatic m_staticSubtitle;

	bool m_isGroupChat = false;
	int64_t m_currentGroupId = 0;
	bool m_rebuildingList = false;
	CButton m_btnCreateGroup;

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MFCCHAT_DIALOG };
#endif
};