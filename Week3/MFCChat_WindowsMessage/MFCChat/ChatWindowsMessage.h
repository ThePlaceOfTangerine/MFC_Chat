#pragma once

#include <afxwin.h>

class ChatWindowsMessage
{
public:
	static BOOL SendMessageToChat(
		HWND hTargetWnd,
		const CString & sender,
		const CString & message
	);
};

