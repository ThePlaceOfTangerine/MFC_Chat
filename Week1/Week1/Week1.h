
// Week1.h : main header file for the PROJECT_NAME application
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CWeek1App:
// See Week1.cpp for the implementation of this class
//

class CWeek1App : public CWinApp
{
public:
	CWeek1App();

// Overrides
public:
	virtual BOOL InitInstance();

// Implementation

	DECLARE_MESSAGE_MAP()
};

extern CWeek1App theApp;
