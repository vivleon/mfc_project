// CanClient.h

#pragma once

#ifndef __AFXWIN_H__
#error "PCH에 대해 이 파일을 포함하기 전에 'pch.h'를 포함합니다."
#endif

#include "resource.h"		// 주 기호입니다.

// [FIX] CCanClientDlg 클래스를 미리 선언합니다. (순환 참조 방지)
class CCanClientDlg;

// CCanClientApp:
// 이 클래스의 구현에 대해서는 CanClient.cpp을(를) 참조하세요.
//
class CCanClientApp : public CWinApp
{
public:
	CCanClientApp();

	// 재정의입니다.
public:
	virtual BOOL InitInstance();

	// [FIX] 설정 Load/Save 함수를 App 클래스로 이동
public:
	void LoadAppSettings(CCanClientDlg* pDlg);
	void SaveAppSettings(CCanClientDlg* pDlg);

	// 구현입니다.
	DECLARE_MESSAGE_MAP()
};

extern CCanClientApp theApp;