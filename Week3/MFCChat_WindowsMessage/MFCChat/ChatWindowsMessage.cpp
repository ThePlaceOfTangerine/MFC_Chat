#include "pch.h"
#include "ChatWindowsMessage.h"

BOOL ChatWindowsMessage::SendMessageToChat(
	HWND hTargetWnd,
	const CString& sender,
	const CString& message)
{
	if (hTargetWnd == NULL)
		return FALSE;

	CString data;
	data.Format(L"%s|%s", sender, message);

	COPYDATASTRUCT cds;

	cds.dwData = 1;
	cds.cbData = (data.GetLength() + 1) * sizeof(wchar_t);
	cds.lpData = (void*)data.GetString();

	LRESULT result = ::SendMessage(
		hTargetWnd,
		WM_COPYDATA,
		0,
		(LPARAM)&cds
	);

	return result != 0;
}
