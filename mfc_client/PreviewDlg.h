#pragma once
#include "afxdialogex.h"

// [NEW] Remove OpenCV and vector dependencies, only GDI+
// #include <vector>
// #include <opencv2/opencv.hpp> 

// CPreviewDlg Dialog
class CPreviewDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CPreviewDlg)

public:
    CPreviewDlg(CWnd* pParent = nullptr);   // standard constructor
    virtual ~CPreviewDlg();

    // [NEW] Set image paths from local disk
    void SetImagePaths(CString strPathTop, CString strPathSide);

    // Dialog Data
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PREVIEW_DLG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint(); // [NEW] Use OnPaint for drawing
    DECLARE_MESSAGE_MAP()

private:
    // [NEW] Store paths
    CString m_strPathTop;
    CString m_strPathSide;

    // [NEW] GDI+ Image objects
    Gdiplus::Image* m_imgTop;
    Gdiplus::Image* m_imgSide;

    // [NEW] Helper functions
    void LoadImageFromFile(CString sPath, Gdiplus::Image** ppImage);
    void DrawImageToCtrl(Gdiplus::Image* pImage, UINT nCtrlID);
};

