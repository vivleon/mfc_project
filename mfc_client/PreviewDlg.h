#pragma once
#include "afxdialogex.h"
#include <vector>
#include <opencv2/opencv.hpp> // OpenCV 필요

// CPreviewDlg 대화 상자

class CPreviewDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CPreviewDlg)

public:
    CPreviewDlg(CWnd* pParent = nullptr);   // 표준 생성자입니다.
    virtual ~CPreviewDlg();

    // 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PREVIEW_DLG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원
    virtual BOOL OnInitDialog();
    DECLARE_MESSAGE_MAP()

private:
    std::vector<unsigned char> m_bufLeft;
    std::vector<unsigned char> m_bufRight;

    // CBitmap 객체는 대화상자가 살아있는 동안 유지되어야 함
    CBitmap m_bmpLeft;
    CBitmap m_bmpRight;

    // OpenCV Mat -> CBitmap 변환 (GDI+)
    bool MatToCBitmap(const cv::Mat& mat, CBitmap& bitmap);
    // 버퍼 -> CBitmap 변환
    bool BufferToCBitmap(const std::vector<unsigned char>& buffer, CBitmap& bitmap);

public:
    // 부모 윈도우에서 이미지 버퍼를 설정
    void SetImageBuffer(const std::vector<unsigned char>& buffer, int index);
};
